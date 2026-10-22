#include "qqmusic_ui.h"
#include "backend.h"
#include <QElapsedTimer>
#include <QQuickItem>
#include <QQuickWindow>
#include <QTemporaryDir>
#include <QTest>
#include <QTimer>
#include <QFile>
#include <QDir>
#include <cstdio>

namespace {
QQuickItem *find(QQuickItem *parent,const QString &name) {
  if(parent->objectName()==name)return parent;
  for(auto child:parent->childItems())if(auto match=find(child,name))return match;
  return nullptr;
}
bool until(const std::function<bool()> &ready,int timeout=8000) {
  QElapsedTimer elapsed;elapsed.start();
  while(!ready() && elapsed.elapsed()<timeout)QTest::qWait(25);
  return ready();
}
}

void runQQMusicUiTests(Backend *b,QQuickWindow *window) {
  int failures=0;
  const auto check=[&](bool ok,const char *label){fprintf(stdout,"%s %s\n",ok?"PASS":"FAIL",label);fflush(stdout);if(!ok)++failures;};
  const auto folder=qEnvironmentVariable("SUNG_TEST_OUTPUT");QDir().mkpath(folder);
  const auto capture=[&](const char *label){QTest::qWait(180);check(window->grabWindow().save(folder+'/'+label+".png"),label);};
  const auto click=[&](const QString &name){auto item=find(window->contentItem(),name);check(item && item->isVisible(),qPrintable("visible "+name));if(item)QTest::mouseClick(window,Qt::LeftButton,Qt::NoModifier,item->mapToScene(QPointF(item->width()/2,item->height()/2)).toPoint());};
  b->setOnboarded(true);b->setMotion(false);b->setVolume(0);b->setWatchMusicFolders(false);b->setAutoplay(false);b->setPrepareNext(false);b->clearQueue();
  window->resize(1180,800);
  b->home();check(until([&]{return !b->busy();}),"QQ home loaded");
  check(!b->sections().isEmpty() && b->title()=="QQ Music","QQ home has recommendations");
  check(find(window->contentItem(),"qqMusicHomeHeader"),"QQ home header is present");
  const auto songPin=find(window->contentItem(),"cardPinAction");check(songPin && !songPin->isVisible(),"song cards have no collection pin action");capture("home");
  b->search("fixture","songs");check(until([&]{return !b->busy();}),"QQ search loaded");
  check(b->results()->count()==30,"QQ search returns songs");
  const auto song=b->results()->get(0);check(song.value("source")=="qqmusic","search source is QQ Music");
  b->playItem(song);check(until([&]{return b->playing();}),"QQ audio is buffered and played");b->pause();
  b->openLink("https://example.invalid/album/unsupported");check(!b->error().isEmpty(),"unsupported online links are rejected");b->dismissError();
  QTemporaryDir credential;
  QFile file(credential.filePath("credential.json"));
  check(file.open(QIODevice::WriteOnly),"fixture credential can be written");
  file.write("{\"musicid\":111,\"musickey\":\"fixture-key\",\"encryptUin\":\"fixture-identity\"}");file.close();
  b->importQQMusicCredential(QUrl::fromLocalFile(file.fileName()));
  b->library("favorites");check(until([&]{return !b->busy();}),"QQ liked songs loaded");
  check(b->results()->count()==2 && b->isLiked(b->results()->get(0).value("id").toString()),"QQ heart state matches the account");capture("liked");
  b->library("playlists");check(until([&]{return !b->busy();}),"QQ playlists loaded");
  check(b->qqMusicPlaylists().size()==2,"created and collected playlists are present");capture("playlists");
  const auto playlist=b->qqMusicPlaylists().first().toMap();
  QTest::qWait(150);click("qqMusicPlaylist_"+playlist.value("remoteId").toString());
  check(until([&]{return b->page()=="playlist" && !b->busy();}),"QQ playlist button opens its tracks");
  check(b->results()->count()==2,"QQ playlist has tracks");b->back();
  check(until([&]{return b->qqMusicLibrary() && !b->busy();}),"Back restores the QQ library");
  b->setViewMode("grid");capture("playlist-grid");b->setTheme("dark");capture("playlist-dark");b->setTheme("light");
  window->resize(480,580);capture("compact-playlists");
  b->clearQQMusicCredential();b->library("favorites");
  check(!b->busy() && b->results()->count()==0,"sign-out clears account rows");capture("sign-in");
  qputenv("SUNG_QQ_LOGIN_FIXTURE","waiting");click("qqMusicLibrarySignIn");
  check(until([&]{return !b->qqMusicLoginArt().isEmpty();}),"in-app QQ QR login opens");capture("qr-login");
  b->cancelQQMusicLogin();qunsetenv("SUNG_QQ_LOGIN_FIXTURE");b->stop();b->clearQueue();
  fprintf(stdout,"RESULT %d failures\n",failures);fflush(stdout);
  if(failures)QCoreApplication::exit(1);
  else {
    window->requestActivate();QTest::qWait(100);
    auto focused=QGuiApplication::focusWindow();QTest::keyClick(focused?focused:window,Qt::Key_Q,Qt::ControlModifier);
    QTimer::singleShot(1000,QCoreApplication::instance(),[]{fprintf(stdout,"FAIL Ctrl+Q did not exit\n");QCoreApplication::exit(2);});
  }
}
