// Songs kept after they have been played, measured on the real store and then
// through the player that fills it.
//
// The claim worth proving is the one a listener would notice: a song played
// once plays again with nothing to fetch it from. The last case takes the
// fetcher away entirely, so it fails if the keeping is removed.
#include "backend.h"
#include "offlinestore.h"
#include <QDir>
#include <QFile>
#include <QSignalSpy>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QtTest>

class OfflineTest : public QObject {
  Q_OBJECT
  QTemporaryDir storage;

  // A file of a given size standing in for a buffered song.
  QString buffered(const QString &name, qint64 bytes) {
    const auto path = QDir(storage.path()).absoluteFilePath(name);
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly))
      return {};
    file.write(QByteArray(int(bytes), 'x'));
    file.close();
    return path;
  }

  // Age a kept file so eviction has an order to work with. The store reads
  // the filesystem's modification time, so the test writes it there too.
  static void age(const QString &path, int secondsAgo) {
    QFile file(path);
    QVERIFY(file.open(QIODevice::ReadOnly));
    file.setFileTime(QDateTime::currentDateTime().addSecs(-secondsAgo),
                     QFileDevice::FileModificationTime);
  }

private slots:
  void initTestCase() {
    qputenv("XDG_DATA_HOME", storage.path().toUtf8());
    qputenv("XDG_CONFIG_HOME", storage.path().toUtf8());
    qputenv("XDG_CACHE_HOME", storage.path().toUtf8());
    QCoreApplication::setApplicationName("sung-offline-test");
    QCoreApplication::setOrganizationName("SungTests");
    qputenv("SUNG_HELPER", qgetenv("SUNG_FIXTURE_HELPER"));
    qputenv("SUNG_PYTHON", "/usr/bin/python3");
  }

  void cleanup() {
    OfflineStore store;
    store.clear();
  }

  // What a song is filed under, and what is never filed at all.
  void whatIsKeptAndUnderWhatName() {
    const QVariantMap imported{{"localPath", "/music/song.flac"}, {"source","qqmusic"},{"remoteId", "abc"}};
    QVERIFY2(OfflineStore::keyFor(imported, "standard").isEmpty(),
             "a file already on disk is not kept a second time");
    QVERIFY2(OfflineStore::keyFor({{"title", "Nothing"}}, "standard").isEmpty(),
             "a song with nothing to fetch it by has no key");

    const QVariantMap track{{"source","qqmusic"},{"remoteId", "abc"}};
    QVERIFY(!OfflineStore::keyFor(track, "standard").isEmpty());
    QVERIFY2(OfflineStore::keyFor(track, "standard") == OfflineStore::keyFor(track, "saver"),
             "QQ Music uses its authorized MP3 quality for every cache key");
    QVERIFY2(OfflineStore::keyFor(track, "standard") != OfflineStore::keyFor({{"source","qqmusic"},{"remoteId", "abd"}}, "standard"),
             "two songs are two keys");

    QVERIFY(OfflineStore::keyFor({{"source","unsupported"},{"id","7"}},"mp3_128").isEmpty());

  }

  // A kept song is found again, and the buffer it came from is gone: the file
  // is moved rather than copied, so keeping it costs no second write.
  void aKeptSongIsFoundAgain() {
    OfflineStore store;
    store.setBudget(4 * 1024 * 1024);
    const auto key = OfflineStore::keyFor({{"source","qqmusic"},{"remoteId", "abc"}}, "standard");
    QVERIFY(store.take(key).isEmpty());
    QVERIFY(!store.has(key));

    const auto source = buffered("abc.opus", 64 * 1024);
    const auto kept = store.keep(key, source);
    QVERIFY2(!kept.isEmpty(), "the song was kept");
    QVERIFY2(!QFile::exists(source), "the buffer was moved, not copied");
    QVERIFY2(kept.endsWith(".opus"), "the container the decoder needs is preserved");
    QVERIFY(store.has(key));
    QCOMPARE(store.take(key), kept);
    QCOMPARE(store.count(), 1);
    QCOMPARE(store.bytes(), qint64(64 * 1024));

    // A second store, as a later run of the application would see it.
    OfflineStore next;
    next.setBudget(4 * 1024 * 1024);
    QCOMPARE(next.take(key), kept);
  }

  // Nothing is kept when the listener has not asked for any disk, and asking
  // for none gives back what was already there.
  void turningItOffKeepsNothingAndGivesBackTheDisk() {
    OfflineStore store;
    store.setBudget(0);
    const auto key = OfflineStore::keyFor({{"source","qqmusic"},{"remoteId", "off"}}, "standard");
    QVERIFY(store.keep(key, buffered("off.opus", 1024)).isEmpty());
    QVERIFY(store.take(key).isEmpty());

    store.setBudget(1024 * 1024);
    QVERIFY(!store.keep(key, buffered("off2.opus", 1024)).isEmpty());
    QCOMPARE(store.count(), 1);
    store.setBudget(0);
    QCOMPARE(store.count(), 0);
    QCOMPARE(store.bytes(), qint64(0));
  }

  // The budget is a promise about disk, so it is kept the moment it is made.
  void theBudgetEvictsTheLeastRecentlyPlayed() {
    OfflineStore store;
    store.setBudget(1024 * 1024);
    QStringList kept;
    for (int i = 0; i < 4; ++i) {
      const auto key = OfflineStore::keyFor({{"source","qqmusic"},{"remoteId", QString("song%1").arg(i)}}, "standard");
      const auto path = store.keep(key, buffered(QString("song%1.opus").arg(i), 200 * 1024));
      QVERIFY(!path.isEmpty());
      kept << path;
      age(path, 400 - i * 100);
    }
    QCOMPARE(store.count(), 4);

    // Playing the oldest one makes it the newest.
    const auto oldest = OfflineStore::keyFor({{"source","qqmusic"},{"remoteId", "song0"}}, "standard");
    QCOMPARE(store.take(oldest), kept.at(0));

    // Room for two: the one just played stays, the next-oldest goes.
    store.setBudget(450 * 1024);
    QCOMPARE(store.count(), 2);
    QVERIFY2(store.has(oldest), "the song just played survived the squeeze");
    QVERIFY2(!store.has(OfflineStore::keyFor({{"source","qqmusic"},{"remoteId", "song1"}}, "standard")),
             "the least recently played went first");
    QVERIFY(store.bytes() <= 450 * 1024);
  }

  // A song that would not fit on its own is played from where it is rather
  // than emptying the store to make room for itself.
  void aSongLargerThanTheBudgetIsNotKept() {
    OfflineStore store;
    store.setBudget(100 * 1024);
    const auto small = OfflineStore::keyFor({{"source","qqmusic"},{"remoteId", "small"}}, "standard");
    QVERIFY(!store.keep(small, buffered("small.opus", 40 * 1024)).isEmpty());
    const auto huge = OfflineStore::keyFor({{"source","qqmusic"},{"remoteId", "huge"}}, "standard");
    const auto source = buffered("huge.opus", 400 * 1024);
    QVERIFY(store.keep(huge, source).isEmpty());
    QVERIFY2(QFile::exists(source), "the caller still has the file it was about to play");
    QVERIFY2(store.has(small), "and the store it could not join was left alone");
  }

  // A copy that turned out not to be playable is dropped, so the song can be
  // fetched again instead of failing on the same file for ever.
  void aCopyThatWillNotPlayIsDropped() {
    OfflineStore store;
    store.setBudget(1024 * 1024);
    const auto key = OfflineStore::keyFor({{"source","qqmusic"},{"remoteId", "broken"}}, "standard");
    QVERIFY(!store.keep(key, buffered("broken.opus", 4096)).isEmpty());
    QVERIFY(store.has(key));
    store.forget(key);
    QVERIFY(!store.has(key));
    QVERIFY(store.take(key).isEmpty());
    QCOMPARE(store.count(), 0);
    // Forgetting something that was never held is not an error.
    store.forget(key);
    store.forget({});
  }

  // The whole point, through the player: a song played once plays again with
  // nothing left to fetch it. Removing the keeping fails this case.
  void aSongPlayedOncePlaysAgainWithNothingToFetchIt() {
    qputenv("SUNG_BUFFER_FIXTURE", "1");
    const QVariantMap song{{"id", "00000000001"}, {"source","qqmusic"},{"remoteId", "00000000001"},
                           {"title", "Track 1"},  {"kind", "song"},
                           {"artist", "Test artist"}, {"seconds", 120},
                           {"available", true}};
    QString first;
    {
      Backend b;
      b.setVolume(0);

      b.setAutoplay(false);
      b.setWatchMusicFolders(false);

      b.setPrepareNext(false);
      b.setKeepPlayedMb(64);
      b.clearQueue();
      b.enqueueItems({song});
      b.playAt(0);
      QTRY_VERIFY_WITH_TIMEOUT(b.playing() && b.position() > 200, 30000);
      first = b.media()->source().toLocalFile();
      OfflineStore store;
      store.setBudget(64 * 1024 * 1024);
      QVERIFY2(first.startsWith(store.root()),
               qPrintable("played from " + first + ", not from " + store.root()));
      QVERIFY2(store.has(OfflineStore::keyFor(song, QString("mp3_128"))),
               "the song is in the store under the key the player will look for");
      b.stop();
    }

    // Take the fetcher away. The fixture now refuses to produce audio, which
    // is what a train or an outage looks like from here.
    qunsetenv("SUNG_BUFFER_FIXTURE");
    {
      Backend b;
      b.setVolume(0);

      b.setAutoplay(false);
      b.setWatchMusicFolders(false);

      b.setPrepareNext(false);
      b.clearQueue();
      b.enqueueItems({song});
      b.playAt(0);
      QTRY_VERIFY_WITH_TIMEOUT(b.playing() && b.position() > 200, 30000);
      QVERIFY2(b.error().isEmpty(), qPrintable("error: " + b.error()));
      QCOMPARE(b.media()->source().toLocalFile(), first);
      b.stop();
    }

    // And with keeping turned off it is gone, so the case above was really
    // measuring the store rather than some other cache.
    {
      Backend b;
      b.setKeepPlayedMb(0);
      OfflineStore store;
      store.setBudget(64 * 1024 * 1024);
      QVERIFY(!store.has(OfflineStore::keyFor(song, QString("mp3_128"))));
      b.setKeepPlayedMb(1024);
    }
  }
};
QTEST_MAIN(OfflineTest)
#include "offline_test.moc"
