#include "desktop_ui.h"
#include "desktopintegration.h"
#include "backend.h"
#include <QApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QImage>
#include <QQuickItem>
#include <QQuickWindow>
#include <QTest>
#include <cstdio>

namespace {
bool until(const std::function<bool()> &ready,int timeout=8000) {
  QElapsedTimer elapsed;elapsed.start();while(!ready() && elapsed.elapsed()<timeout)QTest::qWait(25);return ready();
}
QQuickItem *find(QQuickItem *parent,const QString &name) {
  if(parent->objectName()==name)return parent;
  for(auto child:parent->childItems())if(auto match=find(child,name))return match;
  return nullptr;
}
}
void runDesktopUiTests(Backend *b,QQuickWindow *window,DesktopIntegration *desktop) {
  int failures=0;
  auto check=[&](bool ok,const char *label){fprintf(stdout,"%s %s\n",ok?"PASS":"FAIL",label);fflush(stdout);failures+=!ok;};
  const auto output=qEnvironmentVariable("SUNG_TEST_OUTPUT");QDir().mkpath(output);
  auto capture=[&](QQuickWindow *w,const char *name){QTest::qWait(200);check(w->grabWindow().save(output+'/'+name+".png"),name);};
  b->setOnboarded(true);b->setMotion(false);b->setWatchMusicFolders(false);b->setVolume(0);b->setAutoplay(false);b->setPrepareNext(false);b->setCloseToTray(true);b->clearQueue();
  check(until([&]{return desktop->available();}),"desktop tray host is available");
  b->search("fixture","songs");check(until([&]{return !b->busy();}),"QQ fixture search loaded");
  auto song=b->results()->get(0);
  QImage cover(200,200,QImage::Format_RGB32);cover.fill(QColor("#926135"));const auto coverFile=output+"/desktop-cover.png";cover.save(coverFile);
  song["art"]=QUrl::fromLocalFile(coverFile).toString();
  b->playItem(song);check(until([&]{return b->playing() && b->position()>100;}),"playback started");capture(window,"desktop-playing");
  // The external probe observes Caelestia and MPRIS while the actual player runs.
  QFile ready(output+"/playing.ready");check(ready.open(QIODevice::WriteOnly),"observer marker can be written");ready.write("ready");ready.close();
  check(until([&]{return QFile::exists(output+"/observed.ready");},15000),"external MPRIS and Shell observer finished");
  const auto position=b->position();window->close();
  check(!window->isVisible(),"main window closes to tray");QTest::qWait(400);
  check(b->playing() && b->position()>position,"audio continues with main window hidden");
  QFile hidden(output+"/hidden.ready");check(hidden.open(QIODevice::WriteOnly),"background marker can be written");hidden.write("ready");hidden.close();
  check(until([&]{return QFile::exists(output+"/restored.ready") && window->isVisible();},15000),"tray activation restores the player");
  // Click the real action without activating the background workspace.
  auto miniButton=find(window->contentItem(),"miniPlayerButton");check(miniButton && miniButton->isVisible(),"mini player action is visible");
  if(miniButton)QTest::mouseClick(window,Qt::LeftButton,Qt::NoModifier,miniButton->mapToScene(QPointF(miniButton->width()/2,miniButton->height()/2)).toPoint());
  QQuickWindow *mini=nullptr;
  check(until([&]{for(auto w:QGuiApplication::allWindows())if(w->objectName()=="miniPlayerWindow")mini=qobject_cast<QQuickWindow *>(w);return mini && mini->isVisible();}),"click opens the mini player");
  if(mini) {
    capture(mini,"desktop-mini");
    auto close=find(mini->contentItem(),"miniCloseButton");check(close,"mini close button exists");
    if(close)QTest::mouseClick(mini,Qt::LeftButton,Qt::NoModifier,close->mapToScene(QPointF(close->width()/2,close->height()/2)).toPoint());
    QTest::qWait(300);check(!mini->isVisible() && !window->isVisible() && b->playing(),"mini close keeps audio in the background");
  }
  emit b->raiseRequested();check(until([&]{return window->isVisible();}),"hidden mini player can be restored");
  b->setTheme("dark");window->resize(480,580);capture(window,"desktop-compact-dark");
  fprintf(stdout,"RESULT %d failures\n",failures);fflush(stdout);
  if(failures)QCoreApplication::exit(1);
  // The external observer ends this instance through MPRIS Quit.
}
