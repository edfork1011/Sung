#include "mpris.h"
#include <QtTest>
#include <QDBusArgument>
#include <QDBusConnection>
#include <QDBusMessage>
#include <QDBusPendingCall>
#include <QDBusVariant>
#include <QDataStream>
#include <QFile>
#include <QImage>
#include <QTemporaryDir>

class PropertyObserver : public QObject {
  Q_OBJECT
public:
  bool canPlay=false;
public slots:
  void changed(const QString &interface,const QVariantMap &values,const QStringList &) {
    if(interface=="org.mpris.MediaPlayer2.Player" && values.contains("CanPlay"))canPlay=values.value("CanPlay").toBool();
  }
};

// Runs under dbus-run-session, so the player registers on a private bus and
// the metadata travels over real D-Bus the way a shell reads it.
class MprisMetadataTests : public QObject {
  Q_OBJECT
  QTemporaryDir storage;
  QString folder;
  QUrl audio;
  QString service="org.mpris.MediaPlayer2.sung";
  QVariant properties(const QString &property) {
    // A second connection goes through the bus daemon. The reply needs this
    // thread's event loop, so the call is awaited by spinning it.
    auto client=QDBusConnection::connectToBus(QDBusConnection::SessionBus,"mpris-client");
    auto get=QDBusMessage::createMethodCall(service,"/org/mpris/MediaPlayer2","org.freedesktop.DBus.Properties","Get");
    get<<QString("org.mpris.MediaPlayer2.Player")<<property;
    QDBusPendingCall call=client.asyncCall(get);
    if(!QTest::qWaitFor([&]{return call.isFinished();},5000) || call.isError())return {};
    return call.reply().arguments().value(0).value<QDBusVariant>().variant();
  }
  QVariantMap metadata() {return qdbus_cast<QVariantMap>(properties("Metadata"));}
private slots:
  void initTestCase() {
    QVERIFY(storage.isValid());qputenv("XDG_CONFIG_HOME",storage.path().toUtf8());qputenv("XDG_DATA_HOME",storage.path().toUtf8());
    qputenv("SUNG_HELPER",qgetenv("SUNG_FIXTURE_HELPER"));qputenv("SUNG_PYTHON","python3");
    QCoreApplication::setOrganizationName("SungTests");QCoreApplication::setApplicationName("mpris");
    // Characters a URI has to escape, so the file URI is checked as a URI.
    folder=storage.filePath("Music #100% ü");QVERIFY(QDir().mkpath(folder));
    QFile file(folder+"/song.wav");QVERIFY(file.open(QIODevice::WriteOnly));
    const int bytes=48000*2*4;QDataStream stream(&file);stream.setByteOrder(QDataStream::LittleEndian);
    file.write("RIFF",4);stream<<quint32(36+bytes);file.write("WAVEfmt ",8);stream<<quint32(16)<<quint16(1)<<quint16(1)<<quint32(48000)<<quint32(96000)<<quint16(2)<<quint16(16);file.write("data",4);stream<<quint32(bytes);file.write(QByteArray(bytes,0));file.close();audio=QUrl::fromLocalFile(file.fileName());
  }
  // Each case registers its own player; the name has to be free again.
  void cleanup() {
    auto bus=QDBusConnection::sessionBus();bus.unregisterObject("/org/mpris/MediaPlayer2");bus.unregisterService(service);service="org.mpris.MediaPlayer2.sung";
  }
  // The poster the importer writes carries ?v=<mtime>:<size>. A shell that
  // makes a path of the URI by cutting the scheme must still find the file.
  void localArtworkIsAPlainFile() {
    const auto cover=folder+"/cover_0123.jpg";QImage image(64,64,QImage::Format_RGB32);image.fill(Qt::darkCyan);QVERIFY(image.save(cover));
    Backend b;b.setVolume(0);b.setAutoplay(false);registerMpris(&b);
    b.playItem({{"id","local_0123"},{"kind","song"},{"title","Song"},{"artist","Artist"},{"album","Album"},{"localPath",audio.toLocalFile()},
                {"art",QUrl::fromLocalFile(cover).toString()+"?v=1487355716000000000:33069"}});
    QCOMPARE(b.current().value("id").toString(),QString("local_0123"));
    const auto art=metadata().value("mpris:artUrl").toString();
    QCOMPARE(art,QUrl::fromLocalFile(cover).toString());
    QVERIFY(!QUrl(art).hasQuery());
    QVERIFY(QFileInfo::exists(QUrl(art).toLocalFile()));
    QVERIFY(QFileInfo::exists(QUrl::fromPercentEncoding(art.mid(QString("file://").size()).toUtf8())));
    b.stop();
  }
  // A remote cover goes out as the interface shows it, query included.
  void remoteArtworkKeepsItsQuery() {
    Backend b;b.setVolume(0);b.setAutoplay(false);registerMpris(&b);
    const QString remote="https://y.gtimg.cn/music/photo_new/fixture.jpg?size=600";
    b.playItem({{"id","abcdefghijk"},{"source","qqmusic"},{"remoteId","abcdefghijk"},{"kind","song"},{"title","Song"},{"artist","Artist"},{"album","Album"},{"art",remote}});
    const auto art=metadata().value("mpris:artUrl").toString();
    QCOMPARE(art,b.displayArt(b.current()));
    QVERIFY(QUrl(art).hasQuery());
    b.stop();
  }
  void namedInstanceExportsPlaybackAndChangedCapabilities() {
    service="org.mpris.MediaPlayer2.sung.qqmusic";
    Backend b;b.clearQueue();b.setVolume(0);b.setAutoplay(false);QVERIFY(registerMpris(&b,"qqmusic"));
    QCOMPARE(properties("CanPlay").toBool(),false);
    PropertyObserver observer;
    auto observerBus=QDBusConnection::connectToBus(QDBusConnection::SessionBus,"mpris-observer");
    QVERIFY(observerBus.connect(service,"/org/mpris/MediaPlayer2","org.freedesktop.DBus.Properties","PropertiesChanged",&observer,SLOT(changed(QString,QVariantMap,QStringList))));
    QSignalSpy stateChanges(&b,&Backend::playbackChanged);
    b.playItem({{"id","local_desktop"},{"kind","song"},{"title","Desktop song"},{"artist","Desktop artist"},{"localPath",audio.toLocalFile()}});
    QTRY_VERIFY_WITH_TIMEOUT(b.playing(),5000);
    QCOMPARE(properties("PlaybackStatus").toString(),QString("Playing"));
    QCOMPARE(properties("CanPlay").toBool(),true);QVERIFY(!stateChanges.isEmpty());QTRY_VERIFY(observer.canPlay);
    QCOMPARE(metadata().value("xesam:title").toString(),QString("Desktop song"));
    b.pause();QCOMPARE(properties("PlaybackStatus").toString(),QString("Paused"));
    b.stop();QCOMPARE(properties("PlaybackStatus").toString(),QString("Stopped"));
  }
  void qqMusicMetadataUsesTheQQSongLink() {
    Backend b;b.setVolume(0);b.setAutoplay(false);registerMpris(&b);
    const QString cover="https://y.gtimg.cn/music/photo_new/T002R800x800M000albumMID.jpg";
    b.playItem({{"id","qq_0123"},{"remoteId","songMID"},{"source","qqmusic"},{"kind","song"},{"title","QQ song"},{"artist","QQ artist"},{"art",cover}});
    const auto data=metadata();
    QCOMPARE(data.value("xesam:url").toString(),"https://y.qq.com/n/ryqq/songDetail/songMID");
    QCOMPARE(data.value("xesam:title").toString(),"QQ song");
    QCOMPARE(data.value("mpris:artUrl").toString(),cover);
    b.setAutoplay(true);const auto controls=b.findChild<PlayerAdaptor *>();QVERIFY(controls);QVERIFY(controls->canNext());
    b.stop();
  }
};
QTEST_MAIN(MprisMetadataTests)
#include "mpris_metadata_test.moc"
