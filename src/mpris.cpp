#include "mpris.h"
#include "artworkurl.h"
#include <QCoreApplication>
#include <QDBusConnection>
#include <QDBusMessage>
#include <QDBusError>
void RootAdaptor::Quit() { QCoreApplication::quit(); }
PlayerAdaptor::PlayerAdaptor(Backend *p) : QDBusAbstractAdaptor(p), b(p) {
  connect(p, &Backend::trackChanged, this, &PlayerAdaptor::changed);
  connect(p, &Backend::playbackChanged, this, &PlayerAdaptor::changed);
  connect(p, &Backend::settingsChanged, this, &PlayerAdaptor::changed);
  connect(p, &Backend::artworkChanged, this, &PlayerAdaptor::changed);
  connect(p->queue(), &Entries::countChanged, this, &PlayerAdaptor::changed);
  connect(p->media(), &QMediaPlayer::seekableChanged, this, &PlayerAdaptor::changed);
  connect(p, &Backend::seeked, this, [this](qint64 position){emit Seeked(position*1000);});
}
QVariantMap PlayerAdaptor::metadata() const {
  const auto t = b->current();
  if (t.isEmpty())
    return {};
  return {{"mpris:trackid", QVariant::fromValue(QDBusObjectPath(
                                "/org/mpris/MediaPlayer2/track/" +
                                b->trackToken()))},
          {"mpris:length", b->duration() * 1000},
          {"mpris:artUrl", QVariant(artworkurl::forDesktop(QUrl(b->displayArt(t))).toString())},
          {"xesam:title", t.value("title").toString()},
          {"xesam:artist", QStringList{t.value("artist").toString()}},
          {"xesam:album", t.value("album").toString()},
          {"xesam:url", t.value("source")=="qqmusic"?"https://y.qq.com/n/ryqq/songDetail/"+t.value("remoteId").toString():t.value("localPath").toString().isEmpty()?QString():QUrl::fromLocalFile(t.value("localPath").toString()).toString()}};
}
void PlayerAdaptor::SetPosition(const QDBusObjectPath &id, qlonglong pos) {
  if (canSeek() && pos>=0 && pos<=b->duration()*1000 && id.path() ==
      "/org/mpris/MediaPlayer2/track/" + b->trackToken()) {
    b->seek(pos / 1000);
  }
}
void PlayerAdaptor::changed() {
  auto msg = QDBusMessage::createSignal("/org/mpris/MediaPlayer2",
                                        "org.freedesktop.DBus.Properties",
                                        "PropertiesChanged");
  msg << QString("org.mpris.MediaPlayer2.Player")
      << QVariantMap{{"PlaybackStatus", status()}, {"Metadata", metadata()},
                     {"Rate",rate()}, {"Volume", volume()},         {"Shuffle", shuffle()},
                     {"LoopStatus", loop()},       {"CanSeek", canSeek()},
                     {"CanGoNext",canNext()},{"CanGoPrevious",canPrevious()},
                     {"CanPlay",canControl()},{"CanPause",canControl()}}
      << QStringList{};
  QDBusConnection::sessionBus().send(msg);
}
bool registerMpris(Backend *b, const QString &instance) {
  auto bus = QDBusConnection::sessionBus();
  const auto service = QStringLiteral("org.mpris.MediaPlayer2.sung") + (instance.isEmpty()?QString():"."+instance);
  if (!bus.registerService(service)) {
    qWarning()<<"Cannot register MPRIS player"<<service<<bus.lastError().message();
    return false;
  }
  auto root = new RootAdaptor(b);
  auto player = new PlayerAdaptor(b);
  if (!bus.registerObject("/org/mpris/MediaPlayer2", b, QDBusConnection::ExportAdaptors)) {
    qWarning()<<"Cannot export MPRIS player:"<<bus.lastError().message();
    bus.unregisterService(service);delete player;delete root;return false;
  }
  return true;
}
