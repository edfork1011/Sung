#include "backend.h"
#include "artworkurl.h"
#include "lrc.h"
#include <QStandardPaths>
#include <QDateTime>
#include "rowselection.h"
#include "desktoptheme.h"
#include <QSaveFile>
#include <QClipboard>
#include <QJsonDocument>
#include <QJsonObject>
#include <QImage>
#include <QGuiApplication>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QtTest>
#include <QAbstractItemModelTester>
#include <QRandomGenerator>
#include <limits>
#include <cmath>

class BackendTest : public QObject {
  Q_OBJECT
  QTemporaryDir storage;
  QVariantMap track(const QString &id) {
    return {{"id", id},
            {"source", "qqmusic"}, {"remoteId",id}, {"qqMusicSong",true},
            {"title", "Track " + id},
            {"kind", "song"}};
  }
private slots:
  void initTestCase() {
    qputenv("XDG_DATA_HOME", storage.path().toUtf8());
    qputenv("XDG_CONFIG_HOME", storage.path().toUtf8());
    qputenv("XDG_CACHE_HOME", storage.path().toUtf8());
    QCoreApplication::setApplicationName("sung-test");
    QCoreApplication::setOrganizationName("SungTests");
  }
  void qqMusicIntegration() {
    const auto oldHelper=qgetenv("SUNG_HELPER"),oldPython=qgetenv("SUNG_PYTHON"),oldBuffer=qgetenv("SUNG_BUFFER_FIXTURE"),oldState=qgetenv("SUNG_QQ_ACCOUNT_STATE");
    QTemporaryDir account;
    qputenv("SUNG_QQ_ACCOUNT_STATE",account.filePath("state.json").toUtf8());
    qputenv("SUNG_HELPER",qgetenv("SUNG_FIXTURE_HELPER"));qputenv("SUNG_PYTHON","python3");qputenv("SUNG_BUFFER_FIXTURE","1");
    const auto restore=qScopeGuard([&]{qputenv("SUNG_HELPER",oldHelper);qputenv("SUNG_PYTHON",oldPython);qputenv("SUNG_BUFFER_FIXTURE",oldBuffer);qputenv("SUNG_QQ_ACCOUNT_STATE",oldState);});
    Backend b;b.clearQueue();b.setPrepareNext(false);b.setAutoplay(false);b.library("files");
    const auto credential=account.filePath("credential.json");
    {QFile file(credential);QVERIFY(file.open(QIODevice::WriteOnly));file.write("{\"musicid\":111,\"musickey\":\"fixture-key\",\"encryptUin\":\"fixture-identity\"}");}
    b.importQQMusicCredential(QUrl::fromLocalFile(credential));
    b.search("QQ search","songs");QTRY_VERIFY_WITH_TIMEOUT(!b.busy(),5000);
    QCOMPARE(b.results()->count(),30);QCOMPARE(b.m_request.value("source").toString(),"qqmusic");
    const auto song=b.results()->get(0);QVERIFY(song.value("videoId").toString().isEmpty());
    QCOMPARE(Backend::playable({song,track("sameid00000")}).size(),2);
    auto unavailable=song;unavailable["available"]=false;QVERIFY(Backend::playable({unavailable}).isEmpty());
    auto invalid=song;invalid["remoteId"]="../invalid";QVERIFY(Backend::playable({invalid}).isEmpty());
    b.enqueue(song);QCOMPARE(b.queue()->count(),1);QCOMPARE(b.queue()->data(b.queue()->index(0),Qt::UserRole+1).toString(),"QQ Music");
    RowSelection selection;selection.setModel(b.results());selection.selectAll();QCOMPARE(selection.count(),30);
    b.toggleLike(song);QTRY_VERIFY_WITH_TIMEOUT(b.isLiked(song.value("id").toString()),5000);
    const auto playlist=b.createPlaylist("QQ mixed queue");b.addToPlaylist(playlist,song);b.addToPlaylist(playlist,track("sameid00000"));
    b.openPlaylist(playlist);QCOMPARE(b.results()->count(),2);
    const auto smart=b.saveSmartPlaylist({},"QQ only",{{"source","qqmusic"}});b.openPlaylist(smart);QCOMPARE(b.results()->count(),2);
    b.copyLink(song);QCOMPARE(QGuiApplication::clipboard()->text(),"https://y.qq.com/n/ryqq/songDetail/"+song.value("remoteId").toString());
    b.open({{"kind","album"},{"browseId","QQALBUM"},{"source","qqmusic"},{"title","QQ album"}});QTRY_VERIFY_WITH_TIMEOUT(!b.busy(),5000);
    const auto collection=b.collectionItem();QCOMPARE(collection.value("source").toString(),"qqmusic");QVERIFY(collection.value("id").toString().startsWith("qq_"));
    b.togglePin(collection);QVERIFY(b.isPinned(collection));b.open(b.pins().first().toMap());QTRY_VERIFY_WITH_TIMEOUT(!b.busy(),5000);QCOMPARE(b.m_request.value("source").toString(),"qqmusic");b.togglePin(collection);
    b.playItem(song);QTRY_VERIFY_WITH_TIMEOUT(!b.resolving(),5000);QVERIFY2(b.error().isEmpty(),qPrintable(b.error()));
    QVERIFY(b.m_media().source().isLocalFile());QVERIFY(b.m_offline.has(OfflineStore::keyFor(song,"mp3_128")));
    QCOMPARE(OfflineStore::keyFor(song,"standard"),OfflineStore::keyFor(song,"mp3_128"));
    b.fetchLyrics();QTRY_VERIFY_WITH_TIMEOUT(!b.lyricsBusy(),5000);QCOMPARE(b.lyrics(),"Test lyrics");
    b.radio(song);QTRY_COMPARE_WITH_TIMEOUT(b.queue()->count(),5,5000);
    b.clearQueue();b.deletePlaylist(playlist);b.deletePlaylist(smart);b.toggleLike(song);QTRY_VERIFY_WITH_TIMEOUT(!b.isLiked(song.value("id").toString()),5000);b.library("files");b.clearQQMusicCredential();
  }
  void qqMusicAccountLibrary() {
    QTemporaryDir directory;
    const auto oldHelper=qgetenv("SUNG_HELPER"),oldPython=qgetenv("SUNG_PYTHON"),oldState=qgetenv("SUNG_QQ_ACCOUNT_STATE"),oldMode=qgetenv("SUNG_QQ_ACCOUNT_FIXTURE"),oldLog=qgetenv("SUNG_REQUEST_LOG");
    qputenv("SUNG_HELPER",qgetenv("SUNG_FIXTURE_HELPER"));qputenv("SUNG_PYTHON","python3");qputenv("SUNG_QQ_ACCOUNT_STATE",directory.filePath("state.json").toUtf8());qputenv("SUNG_REQUEST_LOG",directory.filePath("requests.jsonl").toUtf8());qunsetenv("SUNG_QQ_ACCOUNT_FIXTURE");
    const auto restore=qScopeGuard([&]{qputenv("SUNG_HELPER",oldHelper);qputenv("SUNG_PYTHON",oldPython);qputenv("SUNG_QQ_ACCOUNT_STATE",oldState);qputenv("SUNG_QQ_ACCOUNT_FIXTURE",oldMode);qputenv("SUNG_REQUEST_LOG",oldLog);});
    Backend b;b.library("files");b.clearQQMusicCredential();
    const auto local=b.createPlaylist("Local playlist stays local");
    b.library("favorites");QVERIFY(b.qqMusicLibrary());QVERIFY(!b.busy());QCOMPARE(b.results()->count(),0);
    b.library("playlists");QVERIFY(b.qqMusicPlaylists().isEmpty());QVERIFY(!b.busy());
    const auto credential=directory.filePath("credential.json");
    const auto signIn=[&](int identity){QFile file(credential);if(!file.open(QIODevice::WriteOnly|QIODevice::Truncate))return false;file.write(QJsonDocument(QJsonObject{{"musicid",identity},{"musickey","fixture-key"},{"encryptUin","fixture-identity"}}).toJson());file.close();b.importQQMusicCredential(QUrl::fromLocalFile(credential));return true;};
    QVERIFY(signIn(111));QTRY_VERIFY_WITH_TIMEOUT(!b.busy(),5000);
    QCOMPARE(b.qqMusicPlaylists().size(),2);QCOMPARE(b.libraryId(),"playlists");
    const auto playlist=b.qqMusicPlaylists().first().toMap();QCOMPARE(playlist.value("source").toString(),"qqmusic");QVERIFY(playlist.value("owned").toBool());
    QVERIFY(!b.qqMusicPlaylists()[1].toMap().value("owned").toBool());
    b.open(playlist);QTRY_VERIFY_WITH_TIMEOUT(!b.busy(),5000);QCOMPARE(b.page(),"playlist");QCOMPARE(b.m_request.value("id").toString(),"12345");
    b.back();QTRY_VERIFY_WITH_TIMEOUT(!b.busy(),5000);QVERIFY(b.qqMusicLibrary());QCOMPARE(b.qqMusicPlaylists().size(),2);
    b.library("favorites");QTRY_VERIFY_WITH_TIMEOUT(!b.busy(),5000);QCOMPARE(b.results()->count(),2);
    const auto liked=b.results()->get(0);const auto likedId=liked.value("id").toString();QVERIFY(b.isLiked(likedId));
    b.toggleLike(liked);QVERIFY(b.isLiked(likedId));QTRY_VERIFY_WITH_TIMEOUT(!b.isLiked(likedId),5000);QCOMPARE(b.results()->count(),1);
    qputenv("SUNG_QQ_ACCOUNT_FIXTURE","like-error");b.toggleLike(liked);QTRY_VERIFY_WITH_TIMEOUT(!b.error().isEmpty(),5000);QVERIFY(!b.isLiked(likedId));QCOMPARE(b.results()->count(),1);b.dismissError();
    qputenv("SUNG_QQ_ACCOUNT_FIXTURE","paged");b.library("favorites");QTRY_VERIFY_WITH_TIMEOUT(!b.busy(),5000);QCOMPARE(b.results()->count(),100);QVERIFY(b.canMore());
    b.more();QTRY_VERIFY_WITH_TIMEOUT(!b.busy(),5000);QCOMPARE(b.results()->count(),150);QVERIFY(!b.canMore());
    qputenv("SUNG_QQ_ACCOUNT_FIXTURE","partial");b.library("playlists");QTRY_VERIFY_WITH_TIMEOUT(!b.busy(),5000);QVERIFY(!b.qqMusicLibraryNotice().isEmpty());QCOMPARE(b.qqMusicPlaylists().size(),2);
    qputenv("SUNG_QQ_ACCOUNT_FIXTURE","error");b.library("favorites");QTRY_VERIFY_WITH_TIMEOUT(!b.busy(),5000);QVERIFY(b.canRetry());QVERIFY(b.results()->rows.isEmpty());
    qunsetenv("SUNG_QQ_ACCOUNT_FIXTURE");b.retry();QTRY_VERIFY_WITH_TIMEOUT(!b.busy(),5000);QCOMPARE(b.results()->count(),1);
    qputenv("SUNG_QQ_ACCOUNT_FIXTURE","slow");b.library("playlists");QVERIFY(b.busy());b.clearQQMusicCredential();QVERIFY(!b.busy());QVERIFY(b.qqMusicPlaylists().isEmpty());QVERIFY(!b.isLiked(likedId));
    QTest::qWait(1100);QVERIFY(b.qqMusicPlaylists().isEmpty());
    qunsetenv("SUNG_QQ_ACCOUNT_FIXTURE");QVERIFY(signIn(222));QTRY_VERIFY_WITH_TIMEOUT(!b.busy(),5000);
    b.library("favorites");QTRY_VERIFY_WITH_TIMEOUT(!b.busy(),5000);QCOMPARE(b.results()->count(),1);QCOMPARE(b.results()->get(0).value("remoteId").toString(),"qqmid000000007");QVERIFY(!b.isLiked(likedId));
    QFile log(directory.filePath("requests.jsonl"));QVERIFY(log.open(QIODevice::ReadOnly));const auto bytes=log.readAll();QVERIFY(!bytes.contains("fixture-key"));QVERIFY(bytes.contains("credentialFile"));QVERIFY(bytes.contains("qqMusicId"));
    b.library("local-playlists");QVERIFY(!b.qqMusicLibrary());QVERIFY(!b.playlists().isEmpty());b.deletePlaylist(local);b.clearQQMusicCredential();
  }
  void qqMusicHomeRecommendations() {
    const auto oldHelper=qgetenv("SUNG_HELPER"),oldPython=qgetenv("SUNG_PYTHON"),oldMode=qgetenv("SUNG_QQ_HOME_FIXTURE");
    qputenv("SUNG_HELPER",qgetenv("SUNG_FIXTURE_HELPER"));qputenv("SUNG_PYTHON","python3");qputenv("SUNG_QQ_HOME_FIXTURE","recommendations");
    const auto restore=qScopeGuard([&]{qputenv("SUNG_HELPER",oldHelper);qputenv("SUNG_PYTHON",oldPython);qputenv("SUNG_QQ_HOME_FIXTURE",oldMode);});
    Backend b;b.library("files");b.m_pins.clear();b.resetHomeLayout();b.setStartPage("home");
    b.togglePin({{"kind","album"},{"browseId","OLDALBUM"},{"title","Unavailable pin"}});
    b.openStartPage();QTRY_VERIFY_WITH_TIMEOUT(!b.busy(),5000);
    QCOMPARE(b.page(),"home");QCOMPARE(b.title(),"QQ Music");
    QCOMPARE(b.m_request.value("source").toString(),"qqmusic");QCOMPARE(b.sections().size(),4);
    QCOMPARE(b.homeSections().size(),4);QVERIFY(b.homeNotice().isEmpty());
    for(const auto &section:b.homeSections())for(const auto &item:section.toMap().value("items").toList()){
      QCOMPARE(item.toMap().value("source").toString(),"qqmusic");QVERIFY(item.toMap().value("videoId").toString().isEmpty());
    }
    const auto first=b.sections().first().toMap().value("items").toList().first().toMap();
    QVERIFY(Backend::playable({first}).size()==1);
    for(int index:{1,3}){
      const auto card=b.sections()[index].toMap().value("items").toList().first().toMap();
      b.open(card);QTRY_VERIFY_WITH_TIMEOUT(!b.busy(),5000);
      QCOMPARE(b.m_request.value("source").toString(),"qqmusic");QCOMPARE(b.results()->count(),2);
      QCOMPARE(b.m_request.value("id"),card.value("browseId"));
      b.back();QCOMPARE(b.page(),"home");QCOMPARE(b.sections().size(),4);
    }
    qputenv("SUNG_QQ_HOME_FIXTURE","partial");b.home();QTRY_VERIFY_WITH_TIMEOUT(!b.busy(),5000);
    QVERIFY(!b.homeNotice().isEmpty());QCOMPARE(b.sections().size(),4);QVERIFY(b.error().isEmpty());
    b.open(b.sections()[1].toMap().value("items").toList().first().toMap());QTRY_VERIFY_WITH_TIMEOUT(!b.busy(),5000);
    QVERIFY(b.homeNotice().isEmpty());b.back();QVERIFY(!b.homeNotice().isEmpty());
    // Account changes refresh recommendations with the newly stored credential.
    QTemporaryDir directory;const auto credential=directory.filePath("credential.json");
    {QFile file(credential);QVERIFY(file.open(QIODevice::WriteOnly));file.write("{\"musicid\":123,\"musickey\":\"fixture-key\"}");}
    qputenv("SUNG_QQ_HOME_FIXTURE","recommendations");b.importQQMusicCredential(QUrl::fromLocalFile(credential));
    QVERIFY(b.busy());QTRY_VERIFY_WITH_TIMEOUT(!b.busy(),5000);QVERIFY(b.homeNotice().isEmpty());
    b.clearQQMusicCredential();QVERIFY(b.busy());QTRY_VERIFY_WITH_TIMEOUT(!b.busy(),5000);
    // An empty initial load can be retried through the same QQ Music route.
    b.library("files");qputenv("SUNG_QQ_HOME_FIXTURE","error");b.home();QTRY_VERIFY_WITH_TIMEOUT(!b.busy(),5000);
    QVERIFY(b.sections().isEmpty());QVERIFY(b.canRetry());QCOMPARE(b.homeError(),b.error());QVERIFY(!b.homeError().isEmpty());QCOMPARE(b.m_request.value("source").toString(),"qqmusic");
    qputenv("SUNG_QQ_HOME_FIXTURE","recommendations");b.retry();QTRY_VERIFY_WITH_TIMEOUT(!b.busy(),5000);
    QCOMPARE(b.sections().size(),4);QVERIFY(b.error().isEmpty());QVERIFY(b.homeError().isEmpty());
    b.m_pins.clear();b.resetHomeLayout();b.library("files");
  }
  void qqMusicCredentialsStayOutsideExports() {
    Backend b;b.library("files");b.clearQQMusicCredential();QVERIFY(!b.qqMusicCredential());
    QTemporaryDir directory;const auto path=directory.filePath("credential.json");
    {QFile file(path);QVERIFY(file.open(QIODevice::WriteOnly));file.write("{\"musicid\":123456,\"musickey\":\"secret-fixture-key\"}");}
    b.importQQMusicCredential(QUrl::fromLocalFile(path));QVERIFY(b.qqMusicCredential());
    const auto stored=QStandardPaths::writableLocation(QStandardPaths::AppDataLocation)+"/qqmusic-credential.json";
    const auto permissions=QFile::permissions(stored);QVERIFY(!(permissions&QFileDevice::ReadGroup));QVERIFY(!(permissions&QFileDevice::ReadOther));
    const auto exported=directory.filePath("library.json");b.exportLibrary(QUrl::fromLocalFile(exported));QFile output(exported);QVERIFY(output.open(QIODevice::ReadOnly));QVERIFY(!output.readAll().contains("secret-fixture-key"));
    {QFile file(path);QVERIFY(file.open(QIODevice::WriteOnly|QIODevice::Truncate));file.write("{\"musicid\":0,\"musickey\":\"rejected-key\"}");}
    b.importQQMusicCredential(QUrl::fromLocalFile(path));QVERIFY(!b.error().isEmpty());QVERIFY(!b.error().contains("rejected-key"));
    QFile saved(stored);QVERIFY(saved.open(QIODevice::ReadOnly));QVERIFY(saved.readAll().contains("secret-fixture-key"));saved.close();
    b.clearQQMusicCredential();QVERIFY(!b.qqMusicCredential());
  }
  void qqMusicQRLoginLifecycle() {
    const auto oldHelper=qgetenv("SUNG_HELPER"),oldPython=qgetenv("SUNG_PYTHON"),oldMode=qgetenv("SUNG_QQ_LOGIN_FIXTURE");
    qputenv("SUNG_HELPER",qgetenv("SUNG_FIXTURE_HELPER"));qputenv("SUNG_PYTHON","python3");
    const auto restore=qScopeGuard([&]{qputenv("SUNG_HELPER",oldHelper);qputenv("SUNG_PYTHON",oldPython);qputenv("SUNG_QQ_LOGIN_FIXTURE",oldMode);});
    Backend b;b.library("files");b.clearQQMusicCredential();
    qputenv("SUNG_QQ_LOGIN_FIXTURE","waiting");b.startQQMusicLogin("qq");
    QCOMPARE(b.qqMusicLoginStatus(),"loading");QVERIFY(b.qqMusicLoginBusy());
    QTRY_COMPARE_WITH_TIMEOUT(b.qqMusicLoginStatus(),QString("waiting"),5000);
    const auto oldImage=QUrl(b.qqMusicLoginArt()).toLocalFile();QVERIFY(QFileInfo::exists(oldImage));
    QVERIFY(!QImage(oldImage).isNull());
    b.cancelQQMusicLogin();QCOMPARE(b.qqMusicLoginStatus(),"idle");QVERIFY(!b.qqMusicLoginBusy());QVERIFY(b.qqMusicLoginArt().isEmpty());
    QTRY_VERIFY_WITH_TIMEOUT(!QFileInfo::exists(oldImage),3000);QVERIFY(!b.qqMusicCredential());
    // A replacement session cannot receive the old worker's later result.
    qputenv("SUNG_QQ_LOGIN_FIXTURE","success");b.startQQMusicLogin("wx");
    QTRY_COMPARE_WITH_TIMEOUT(b.qqMusicLoginStatus(),QString("done"),5000);
    QVERIFY(b.qqMusicCredential());QVERIFY(!b.qqMusicLoginBusy());QVERIFY(b.qqMusicLoginArt().isEmpty());QVERIFY(b.qqMusicLoginError().isEmpty());
    const auto path=QStandardPaths::writableLocation(QStandardPaths::AppDataLocation)+"/qqmusic-credential.json";
    QFile saved(path);QVERIFY(saved.open(QIODevice::ReadOnly));const auto original=saved.readAll();saved.close();
    QCOMPARE(QJsonDocument::fromJson(original).object().value("musicid").toInt(),222);
    QVERIFY(!(QFile::permissions(path)&QFileDevice::ReadOther));
    for(const auto &mode:{"expired","refused","invalid-credential","invalid-image"}) {
      qputenv("SUNG_QQ_LOGIN_FIXTURE",mode);b.startQQMusicLogin("qq");
      QTRY_VERIFY_WITH_TIMEOUT(!b.qqMusicLoginBusy(),5000);
      QVERIFY(!b.qqMusicLoginError().isEmpty());QVERIFY(b.qqMusicLoginArt().isEmpty());
      QCOMPARE(b.qqMusicLoginStatus(),mode==QString("expired")?"expired":mode==QString("refused")?"refused":"error");
      QVERIFY(saved.open(QIODevice::ReadOnly));QCOMPARE(saved.readAll(),original);saved.close();
    }
    b.startQQMusicLogin("unknown");QVERIFY(!b.qqMusicLoginBusy());
    b.clearQQMusicCredential();
  }
  void sleepFadeRestoresUserVolume() {
    Backend b;b.setSleepFade(true);b.setVolume(.6);b.setSleep(15);
    QVERIFY(b.m_sleepFadeStart.isActive());QVERIFY(!b.m_sleepFadeTick.isActive());
    b.m_sleepTimer.start(15000);b.updateSleepGain();
    QVERIFY(qAbs(b.activeAudio().volume()-.3)<.02);QCOMPARE(b.volume(),.6);
    QCOMPARE(b.m_settings.value("volume").toDouble(),.6);
    b.setVolume(.4);QVERIFY(qAbs(b.activeAudio().volume()-.2)<.02);QCOMPARE(b.volume(),.4);
    b.setSleepFade(false);QVERIFY(qAbs(b.activeAudio().volume()-.4)<.001);QVERIFY(!b.m_sleepFadeStart.isActive());
    b.setSleepFade(true);b.m_sleepTimer.start(500);b.updateSleepGain();QVERIFY(b.activeAudio().volume()<.01);
    QTRY_COMPARE_WITH_TIMEOUT(b.sleepLabel(),QString("Off"),2000);
    QVERIFY(qAbs(b.activeAudio().volume()-.4)<.001);QCOMPARE(b.volume(),.4);QVERIFY(!b.m_sleepFadeTick.isActive());
    b.setSleep(15);b.m_sleepTimer.start(5000);b.updateSleepGain();b.setSleep(0);
    QVERIFY(qAbs(b.activeAudio().volume()-.4)<.001);QVERIFY(!b.m_sleepFadeStart.isActive());
    {Backend restored;QCOMPARE(restored.volume(),.4);QVERIFY(restored.sleepFade());}
  }
  void collapsedSelectionExcludesRanges() {
    Entries model;model.reconcile({track("fold0000001"),track("fold0000002"),track("fold0000003"),track("fold0000004")});
    RowSelection selected;selected.setModel(&model);selected.setExcludedRows({1,2});
    selected.selectAll();QCOMPARE(selected.rows(),QVariantList({0,3}));
    selected.clear();selected.select(0);selected.select(3,Qt::ShiftModifier);QCOMPARE(selected.rows(),QVariantList({0,3}));
    selected.setExcludedRows({});selected.selectAll();QCOMPARE(selected.count(),4);
  }
  void listeningRefinements() {
    Backend b;b.clearQueue();b.m_settings.remove("viewLayouts");b.setCompactDensity(false);b.library("local-albums");
    QVERIFY(b.viewSupportsGrid());QCOMPARE(b.viewMode(),"grid");b.setViewMode("list");b.setViewDensity(1);QVERIFY(b.viewCompactDensity());
    b.library("files");QCOMPARE(b.viewDensity(),-1);QVERIFY(!b.viewCompactDensity());QVERIFY(!b.viewSupportsGrid());b.setViewMode("grid");QCOMPARE(b.viewMode(),"list");b.setViewDensity(0);
    b.library("local-albums");QCOMPARE(b.viewMode(),"list");QVERIFY(b.viewCompactDensity());b.setViewDensity(12);QCOMPARE(b.viewDensity(),1);
    {Backend restored;restored.library("local-albums");QCOMPARE(restored.viewMode(),"list");QCOMPARE(restored.viewDensity(),1);}
    b.enqueue(track("origin00001"));QCOMPARE(b.queue()->data(b.queue()->index(0),Qt::UserRole+4).toString(),"Added by you");
    b.m_queue.append(Backend::queueWithOrigin({track("origin00002")},"collection"));b.m_queue.append(Backend::queueWithOrigin({track("origin00003")},"autoplay"));
    QCOMPARE(b.queue()->data(b.queue()->index(1),Qt::UserRole+4).toString(),"Collection");QCOMPARE(b.queue()->data(b.queue()->index(2),Qt::UserRole+4).toString(),"Autoplay");
    b.moveQueue(2,0);QCOMPARE(b.queue()->data(b.queue()->index(0),Qt::UserRole+4).toString(),"Autoplay");b.removeQueue(0);b.undo();QCOMPARE(b.queue()->data(b.queue()->index(0),Qt::UserRole+4).toString(),"Autoplay");b.save();
    {Backend restored;QCOMPARE(restored.queue()->get(0).value("_queueOrigin").toString(),"autoplay");}
    b.m_media().setSource({});b.m_savedPosition=0;
    b.m_lyricLines={QVariantMap{{"start",10000},{"end",15000},{"text","First"}},QVariantMap{{"start",25000},{"end",30000},{"text","Second"}}};
    QCOMPARE(b.lyricGapSeconds(),10);b.m_savedPosition=12000;QCOMPARE(b.lyricGapSeconds(),0);b.m_savedPosition=17000;QCOMPARE(b.lyricGapSeconds(),8);b.m_savedPosition=25000;QCOMPARE(b.lyricGapSeconds(),0);b.m_savedPosition=31000;QCOMPARE(b.lyricGapSeconds(),0);
    b.m_lyricLines[0]=QVariantMap{{"start",0},{"end",0},{"text","Long line without end"}};b.m_savedPosition=10000;QCOMPARE(b.lyricGapSeconds(),0);
    b.m_lyricLines[0]=QVariantMap{{"start",0},{"end",25000},{"text",""}};QCOMPARE(b.lyricGapSeconds(),15);
    b.m_settings.remove("viewLayouts");b.clearQueue();b.setCompactDensity(false);
  }
  void presentationPreferences() {
    Backend b;b.setWatchMusicFolders(false);b.m_pins.clear();b.m_page="home";
    b.m_sections={QVariantMap{{"title","One"},{"items",QVariantList{track("11111111111")}}},QVariantMap{{"title","Two"}},QVariantMap{{"title","Three"}}};
    b.resetHomeLayout();b.setCompactDensity(true);b.setStartPage("files");QVERIFY(b.compactDensity());QCOMPARE(b.startPage(),QString("files"));
    b.setStartPage("invalid");QCOMPARE(b.startPage(),QString("files"));
    b.showHomeSection("One",false);QCOMPARE(b.homeSections().size(),2);QCOMPARE(b.homeSections(true).size(),3);
    b.moveHomeSection("Three",-1);QCOMPARE(b.homeSections()[0].toMap().value("title").toString(),QString("Three"));
    b.showHomeSection("unknown",false);QCOMPARE(b.hiddenHomeSections(),QStringList{"One"});
    {Backend restored;restored.setWatchMusicFolders(false);QVERIFY(restored.compactDensity());QCOMPARE(restored.startPage(),QString("files"));QCOMPARE(restored.homeOrder(),b.homeOrder());QCOMPARE(restored.hiddenHomeSections(),b.hiddenHomeSections());}
    b.openStartPage();QCOMPARE(b.page(),QString("library"));QCOMPARE(b.libraryId(),QString("files"));
    b.m_settings.setValue("startPage","retired-source");QCOMPARE(b.startPage(),QString("home"));b.openStartPage();QCOMPARE(b.page(),QString("home"));
    b.setStartPage("home");b.setCompactDensity(false);b.resetHomeLayout();QVERIFY(b.hiddenHomeSections().isEmpty());QVERIFY(b.homeOrder().isEmpty());
  }
  void playbackPolish() {
    Backend b;b.setPauseOnDisconnect(false);b.setWatchMusicFolders(false);b.m_sessions.clear();b.clearQueue();b.setVolume(0);b.setAutoplay(false);b.setPrepareNext(false);
    QTemporaryDir dir;const auto path=dir.filePath("track.wav");QProcess encode;encode.start("ffmpeg",{"-nostdin","-v","error","-f","lavfi","-i","sine=frequency=330:sample_rate=22050","-t","60",path});QVERIFY(encode.waitForFinished(10000));QCOMPARE(encode.exitCode(),0);
    auto song=track("local_session");song["localPath"]=path;song["seconds"]=60;song["album"]="Test album";song["artist"]="Artist";
    b.playItem(song);QTRY_VERIFY_WITH_TIMEOUT(b.playing(),5000);b.seek(12000);b.pause();b.setPlaybackRate(1.25);
    QVERIFY(b.saveSession("Focus"));const auto id=b.sessions().first().toMap().value("id").toString();QCOMPARE(b.sessions().size(),1);QVERIFY(!b.sessions().first().toMap().contains("queue"));
    b.renameSession(id,"Focus renamed");QCOMPARE(b.sessions().first().toMap().value("title").toString(),QString("Focus renamed"));
    b.setCurrentArtworkFit(true);QVERIFY(b.currentArtworkFit());auto sibling=song;sibling["id"]="local_sibling";QVERIFY(b.artworkFits(sibling));sibling["album"]="Other album";QVERIFY(!b.artworkFits(sibling));
    b.save();{Backend copy;copy.setWatchMusicFolders(false);QCOMPARE(copy.sessions().size(),1);QVERIFY(copy.artworkFits(song));}
    b.clearQueue();QVERIFY(b.restoreSession(id));QTRY_VERIFY_WITH_TIMEOUT(b.playing()&&b.position()>=12000,5000);QVERIFY(b.position()<14000);QCOMPARE(b.playbackRate(),1.25);
    b.setMotion(false);QTRY_COMPARE_WITH_TIMEOUT(b.m_decodeRate,22050,5000);QVERIFY(!b.trackDetails(song).isEmpty());
    b.m_settings.setValue("pauseOnDisconnect",true);b.m_outputDescription="Test headphones";
    b.inspectOutputPorts({QVariantMap{{"description","Test headphones"},{"active_port","analog-output-headphones"}}});QVERIFY(b.playing());
    b.inspectOutputPorts({QVariantMap{{"description","Test headphones"},{"active_port","analog-output-speaker"}}});QVERIFY(!b.playing());
    b.play();QTRY_VERIFY(b.playing());b.m_outputId="removed-device";b.outputsChanged();QVERIFY(!b.playing());
    b.m_settings.setValue("pauseOnDisconnect",false);b.play();QTRY_VERIFY(b.playing());b.m_outputId="removed-device";b.outputsChanged();QVERIFY(b.playing());
    auto saved=b.m_sessions.first().toMap();auto unavailable=song;unavailable["available"]=false;
    saved["queue"]=QVariantList{song,unavailable,song};saved["index"]=2;b.m_sessions[0]=saved;
    QVERIFY(b.restoreSession(id));QCOMPARE(b.currentIndex(),1);QTRY_VERIFY(b.playing());
    if(!QStandardPaths::findExecutable("pactl").isEmpty()){
      b.setPauseOnDisconnect(true);b.setPauseOnDisconnect(false);b.setPauseOnDisconnect(true);
      QTRY_COMPARE_WITH_TIMEOUT(b.m_portMonitor.state(),QProcess::Running,4000);
      b.setPauseOnDisconnect(false);QTRY_COMPARE(b.m_portMonitor.state(),QProcess::NotRunning);
    }
    b.pause();QVERIFY(b.saveSession("Updated",id));QCOMPARE(b.sessions().size(),1);QVERIFY(!b.restoreSession("missing"));b.deleteSession(id);QVERIFY(b.sessions().isEmpty());b.setCurrentArtworkFit(false);b.setMotion(true);b.clearQueue();b.save();
  }
  void libraryPolish() {
    Backend b;b.setWatchMusicFolders(false);b.clearQueue();b.m_localTracks.clear();b.m_playlists.clear();
    auto a=track("local_one"),c=track("local_two"),d=track("local_three");
    for(auto *t:{&a,&c,&d}){t->remove("videoId");(*t)["localPath"]="/test/"+t->value("id").toString()+".flac";(*t)["album"]="Shared title";(*t)["artist"]="Artist A";(*t)["albumArtist"]="Compilation";(*t)["seconds"]=120;}
    a["discNumber"]=2;c["discNumber"]=1;c["trackNumber"]=2;d["albumArtist"]="Other artist";
    b.m_localTracks={a,c,d};auto albums=b.localGroups("local-albums");QCOMPARE(albums.size(),2);
    QVariantMap compilation;for(const auto &v:albums)if(v.toMap().value("artist")=="Compilation")compilation=v.toMap();
    QCOMPARE(compilation.value("count").toInt(),2);QCOMPARE(b.localGroupRows(compilation).first().toMap().value("id"),c.value("id"));
    b.library("local-albums");QCOMPARE(b.results()->count(),2);QSignalSpy unchanged(&b,&Backend::catalogChanged);emit b.libraryChanged();QCOMPARE(unchanged.count(),0);b.open(compilation);QCOMPARE(b.page(),QString("local-album"));QCOMPARE(b.results()->count(),2);QVERIFY(b.albumInfo().value("multipleDiscs").toBool());
    b.m_localTracks.removeLast();emit b.libraryChanged();b.back();QCOMPARE(b.results()->count(),1);
    b.library("local-artists");QCOMPARE(b.results()->count(),1);b.open(b.results()->get(0));QCOMPARE(b.page(),QString("local-artist"));QCOMPARE(b.results()->count(),2);
    auto missing=a;missing["artist"]="";missing["albumArtist"]="";missing["album"]="";b.m_localTracks={missing};
    QCOMPARE(b.localGroups("local-albums").first().toMap().value("title").toString(),QString("Unknown album"));
    QCOMPARE(b.localGroups("local-artists").first().toMap().value("title").toString(),QString("Unknown artist"));
    QTemporaryDir dir;QImage image(800,400,QImage::Format_RGB32);image.fill(Qt::red);for(int y=0;y<400;++y)for(int x=400;x<800;++x)image.setPixelColor(x,y,Qt::blue);
    const auto file=dir.filePath("cover.png");QVERIFY(image.save(file));const auto id=b.createPlaylist("Crop test");b.addItemsToPlaylist(id,{a});
    QVERIFY(b.preparePlaylistCover(QUrl("https://example.invalid/image.png")).isEmpty());QVERIFY(b.preparePlaylistCover(QUrl::fromLocalFile(dir.filePath("missing.png"))).isEmpty());
    const auto preview=b.preparePlaylistCover(QUrl::fromLocalFile(file));QVERIFY(!preview.isEmpty());
    QVERIFY(!b.setPlaylistCover("missing",preview));QVERIFY(!b.setPlaylistCover(id,preview,std::numeric_limits<double>::quiet_NaN()));
    QVERIFY(b.setPlaylistCover(id,preview,1,0.5,1));auto cover=b.playlists().first().toMap().value("customCover").toString();
    QImage cropped(QUrl(cover).toLocalFile());QCOMPARE(cropped.size(),QSize(512,512));QVERIFY(cropped.pixelColor(256,256).blue()>240);
    QVERIFY(QFileInfo(QUrl(cover).toLocalFile()).size()<262144);QCOMPARE(b.playlists().first().toMap().value("artworks").toStringList(),QStringList{cover});
    b.openPlaylist(id);QCOMPARE(b.cover(),cover);b.save();
    {Backend restored;restored.setWatchMusicFolders(false);QCOMPARE(restored.playlists().first().toMap().value("customCover").toString(),cover);}
    b.resetPlaylistCover(id);QVERIFY(b.cover().isEmpty());QVERIFY(b.playlists().first().toMap().value("customCover").toString().isEmpty());
    b.setArtworkAccent(true);QVERIFY(b.artworkAccent());b.setArtworkAccent(false);QVERIFY(!b.artworkAccent());
    b.m_localTracks.clear();b.m_playlists.clear();b.save();
  }
  void productFeatures() {
    const auto oldHelper=qgetenv("SUNG_HELPER"),oldPython=qgetenv("SUNG_PYTHON");
    qputenv("SUNG_HELPER",qgetenv("SUNG_FIXTURE_HELPER"));qputenv("SUNG_PYTHON","/usr/bin/python3");
    const auto restore=qScopeGuard([&]{qputenv("SUNG_HELPER",oldHelper);qputenv("SUNG_PYTHON",oldPython);});
    Backend b;b.clearQueue();b.setWatchMusicFolders(false);b.m_musicFolders.clear();b.m_localTracks.clear();b.m_settings.remove("artworkChoices");
    auto one=track("polish00001");one["seconds"]=120;one["artist"]="Album artist";one["discNumber"]=1;
    auto two=track("polish00002");two["seconds"]=180;two["discNumber"]=2;
    b.m_page="album";b.m_request={{"year","2026"}};b.m_results.assign({one,two});
    QCOMPARE(b.albumInfo().value("artist").toString(),QString("Album artist"));
    QCOMPARE(b.albumInfo().value("summary").toString(),QString("2026 · 2 tracks · 5 min"));
    QVERIFY(b.albumInfo().value("multipleDiscs").toBool());
    b.m_page="search";QVERIFY(b.albumInfo().isEmpty());
    one["motionArt"]="file:///cached.mp4";b.m_queue.assign({one,two});b.m_index=0;
    QCOMPARE(b.currentMotionArt(),QString("file:///cached.mp4"));
    b.rejectArtwork();QVERIFY(b.currentMotionArt().isEmpty());
    b.m_index=1;QVERIFY(b.artworkChoice().isEmpty());b.m_index=0;QCOMPARE(b.artworkChoice(),QString("disabled"));
    b.resetArtworkChoice();QVERIFY(b.artworkChoice().isEmpty());
    QTemporaryDir music;const auto original=music.filePath("One.wav");
    QProcess encode;encode.start("ffmpeg",{"-nostdin","-v","error","-f","lavfi","-i","anullsrc=r=8000:cl=mono","-t","1",original});QVERIFY(encode.waitForFinished(10000));QCOMPARE(encode.exitCode(),0);
    b.importMusicFolder(QUrl::fromLocalFile(music.path()));QTRY_VERIFY_WITH_TIMEOUT(!b.importingLocal(),10000);QCOMPARE(b.m_localTracks.size(),1);
    b.setWatchMusicFolders(true);QTRY_VERIFY_WITH_TIMEOUT(b.m_folderWatcher.files().contains(original),5000);
    const auto second=music.filePath("Two.wav");QVERIFY(QFile::copy(original,second));
    QTRY_COMPARE_WITH_TIMEOUT(b.m_localTracks.size(),2,10000);
    QVERIFY(QFile::remove(second));
    QTRY_VERIFY_WITH_TIMEOUT(!b.m_localTracks.last().toMap().value("available",true).toBool(),10000);
    QVERIFY(QFile::copy(original,second));
    QTRY_VERIFY_WITH_TIMEOUT(b.m_localTracks.last().toMap().value("available").toBool(),10000);
    QDir().mkpath(music.filePath("Nested"));QVERIFY(QFile::copy(original,music.filePath("Nested/Three.wav")));
    QTRY_COMPARE_WITH_TIMEOUT(b.m_localTracks.size(),3,10000);
    b.setWatchMusicFolders(false);QVERIFY(b.m_folderWatcher.files().isEmpty());QVERIFY(b.m_folderWatcher.directories().isEmpty());
    QVERIFY(QFile::copy(original,music.filePath("Four.wav")));QTest::qWait(1800);QCOMPARE(b.m_localTracks.size(),3);
    b.forgetMusicFolder(music.path());b.m_localTracks.clear();b.clearQueue();
  }


  void measuredAudioBands() {
    const auto pcm=[](double frequency,double amplitude,QAudioFormat::SampleFormat type,bool inverseStereo=false){
      QAudioFormat f;f.setSampleRate(48000);f.setChannelCount(inverseStereo?2:1);f.setSampleFormat(type);
      QByteArray data(f.bytesForFrames(9600),Qt::Uninitialized);
      for(int i=0;i<9600;++i)for(int c=0;c<f.channelCount();++c){
        const double x=amplitude*std::sin(2*3.14159265358979323846*frequency*i/48000)*(c?-1:1);
        const int at=i*f.channelCount()+c;
        switch(type){case QAudioFormat::Float:reinterpret_cast<float*>(data.data())[at]=x;break;
          case QAudioFormat::Int16:reinterpret_cast<qint16*>(data.data())[at]=qRound(x*32767);break;
          case QAudioFormat::Int32:reinterpret_cast<qint32*>(data.data())[at]=qint32(x*2147483647);break;
          case QAudioFormat::UInt8:reinterpret_cast<quint8*>(data.data())[at]=qRound(128+x*127);break;
          default:break;}
      }
      return QAudioBuffer(data,f);
    };
    const std::array<double,5> frequencies{100,350,1200,3500,10000};
    for(auto format:{QAudioFormat::Float,QAudioFormat::Int16,QAudioFormat::Int32,QAudioFormat::UInt8}){
      for(int band=0;band<5;++band){AudioLevels analyzer;analyzer.process(pcm(frequencies[band],0.45,format,true));const auto levels=analyzer.takeLevels();QCOMPARE(levels.size(),5);for(int other=0;other<5;++other)if(other!=band)QVERIFY2(levels[band].toDouble()>levels[other].toDouble(),"The matching frequency band should dominate, including inverse-phase stereo");}
      AudioLevels silence;silence.process(pcm(100,0,format));QCOMPARE(silence.takeLevels(),QVariantList({0.,0.,0.,0.,0.}));
    }
    AudioLevels quiet,loud;quiet.process(pcm(1200,.02,QAudioFormat::Float));loud.process(pcm(1200,.45,QAudioFormat::Float));QVERIFY(loud.takeLevels()[2].toDouble()>quiet.takeLevels()[2].toDouble()+.3);
    AudioLevels decay;decay.process(pcm(100,.45,QAudioFormat::Float));decay.takeLevels();for(int i=0;i<3;++i){decay.process(pcm(100,0,QAudioFormat::Float));decay.takeLevels();}QCOMPARE(decay.takeLevels(),QVariantList({0.,0.,0.,0.,0.}));
  }
  void measuredSpectrum() {
    // The visualizer's bands are pitch-spaced from 40 Hz to 16 kHz, so a tone
    // lands in the band whose range holds it, and a higher tone further up.
    const auto tone=[](double frequency,double amplitude,int channels=1,bool inverse=false,int frames=9600){
      QAudioFormat f;f.setSampleRate(48000);f.setChannelCount(channels);f.setSampleFormat(QAudioFormat::Float);
      QByteArray data(f.bytesForFrames(frames),Qt::Uninitialized);
      for(int i=0;i<frames;++i)for(int c=0;c<channels;++c)
        reinterpret_cast<float*>(data.data())[i*channels+c]=float(amplitude*std::sin(2*3.14159265358979323846*frequency*i/48000)*(inverse&&c?-1:1));
      return QAudioBuffer(data,f);
    };
    const auto bandOf=[](double frequency,int bands){return int(std::floor(bands*std::log(frequency/40)/std::log(16000.0/40)));};
    const auto loudest=[](const QVariantList &values){int best=0;for(int i=1;i<values.size();++i)if(values[i].toDouble()>values[best].toDouble())best=i;return best;};
    int previous=-1;
    for(double frequency:{100.0,440.0,1200.0,3500.0,10000.0}){
      AudioSpectrum spectrum;spectrum.process(tone(frequency,0.45));
      const auto bands=spectrum.take();
      QCOMPARE(bands.size(),36);
      const int peak=loudest(bands);
      QVERIFY2(qAbs(peak-bandOf(frequency,36))<=1,qPrintable(QString("%1 Hz peaks in band %2, expected %3").arg(frequency).arg(peak).arg(bandOf(frequency,36))));
      QVERIFY2(peak>previous,"a higher tone peaks further up the ring");previous=peak;
      QVERIFY(bands[peak].toDouble()>0.5);
    }
    AudioSpectrum full;full.process(tone(1000,1.0));QVERIFY(full.take()[bandOf(1000,36)].toDouble()>0.99);
    AudioSpectrum quiet,loud;quiet.process(tone(440,0.02));loud.process(tone(440,0.45));
    QVERIFY(loud.take()[bandOf(440,36)].toDouble()>quiet.take()[bandOf(440,36)].toDouble()+0.3);
    // Out-of-phase stereo is transformed per channel, so it does not cancel.
    AudioSpectrum stereo;stereo.process(tone(440,0.45,2,true));QVERIFY(stereo.take()[bandOf(440,36)].toDouble()>0.5);
    AudioSpectrum silence;silence.process(tone(440,0));for(const auto &value:silence.take())QCOMPARE(value.toDouble(),0.0);
    // Less than half a window is not yet a spectrum.
    AudioSpectrum early;early.process(tone(440,0.45,1,false,512));for(const auto &value:early.take())QCOMPARE(value.toDouble(),0.0);
    AudioSpectrum cleared;cleared.process(tone(440,0.45));cleared.reset();for(const auto &value:cleared.take())QCOMPARE(value.toDouble(),0.0);
  }
  void audioBandsFollowPlayback() {
    QTemporaryDir music;const auto path=music.filePath("bands.wav");
    QProcess encode;encode.start("ffmpeg",{"-nostdin","-v","error","-f","lavfi","-i","aevalsrc=if(lt(t\\,1.5)\\,0.45*sin(2*PI*100*t)\\,if(lt(t\\,3)\\,0\\,if(lt(t\\,4.5)\\,0.45*sin(2*PI*3500*t)\\,0))):s=48000:d=6","-c:a","pcm_s16le",path});QVERIFY(encode.waitForFinished(10000));QCOMPARE(encode.exitCode(),0);
    Backend b;b.setVolume(0);b.setMotion(true);b.setUiActive(true);b.setAutoplay(false);b.setPrepareNext(false);b.clearQueue();
    b.playItem({{"id","local_audio_bands"},{"localPath",path},{"kind","song"},{"title","Band fixture"}});
    QTRY_VERIFY_WITH_TIMEOUT(b.playing() && b.audioLevels()[0].toDouble()>.3,5000);
    QVERIFY(b.audioLevels()[0].toDouble()>b.audioLevels()[3].toDouble());
    b.pause();QCOMPARE(b.audioLevels(),QVariantList({0.,0.,0.,0.,0.}));b.toggle();b.seek(1900);
    QTest::qWait(250);QTRY_COMPARE_WITH_TIMEOUT(b.audioLevels(),QVariantList({0.,0.,0.,0.,0.}),700);
    b.seek(3300);QTRY_VERIFY_WITH_TIMEOUT(b.audioLevels()[3].toDouble()>.3,1500);QVERIFY(b.audioLevels()[3].toDouble()>b.audioLevels()[0].toDouble());
    b.setMotion(false);QCOMPARE(b.audioLevels(),QVariantList({0.,0.,0.,0.,0.}));QTest::qWait(100);QCOMPARE(b.audioLevels(),QVariantList({0.,0.,0.,0.,0.}));
    // The spectrum is only measured while something asks for it.
    b.setMotion(true);b.seek(200);QTRY_VERIFY_WITH_TIMEOUT(b.audioLevels()[0].toDouble()>.3,1500);
    for(const auto &value:b.audioSpectrum())QCOMPARE(value.toDouble(),0.0);
    b.setSpectrumActive(true);QTRY_VERIFY_WITH_TIMEOUT(b.audioSpectrum()[5].toDouble()>.3,1500);
    QVERIFY(b.audioSpectrum()[5].toDouble()>b.audioSpectrum()[26].toDouble());
    b.setSpectrumActive(false);for(const auto &value:b.audioSpectrum())QCOMPARE(value.toDouble(),0.0);
    QTest::qWait(100);for(const auto &value:b.audioSpectrum())QCOMPARE(value.toDouble(),0.0);
    b.setUiActive(false);QCOMPARE(b.audioLevels(),QVariantList({0.,0.,0.,0.,0.}));QTest::qWait(100);QCOMPARE(b.audioLevels(),QVariantList({0.,0.,0.,0.,0.}));b.stop();b.clearQueue();b.setUiActive(true);
  }
  void animatedQueueAndSeekPreview() {
    Entries rows;QAbstractItemModelTester modelTest(&rows,QAbstractItemModelTester::FailureReportingMode::QtTest);
    QRandomGenerator random(812);
    for(int n=0;n<150;++n){QVariantList wanted;for(int i=0,count=random.bounded(40);i<count;++i)wanted.append(track(QString::number(random.bounded(12))));rows.reconcile(wanted);QCOMPARE(rows.rows,wanted);}
    rows.assign({track("one"),track("two"),track("three")});QPersistentModelIndex retained(rows.index(1));
    rows.reconcile({track("two"),track("one"),track("three")});QCOMPARE(retained.row(),0);QCOMPARE(retained.data(Qt::UserRole).toMap().value("id").toString(),QString("two"));
    RowSelection selection;selection.setModel(&rows);selection.select(0,0);rows.reconcile({track("one"),track("three"),track("two")});QCOMPARE(selection.count(),0);
    QVariantList large;for(int i=0;i<600;++i)large.append(track(QString::number(i)));rows.reconcile(large);QCOMPARE(rows.rows,large);
    Backend b;b.m_lyricLines=Lrc::parse("[00:02] First line\n[00:07] Second line",12000);b.m_queue.assign({track("preview0001")});b.m_index=0;b.m_lyricOffsets.clear();
    QCOMPARE(b.previewLyric(1000),QString());QCOMPARE(b.previewLyric(2000),QString("First line"));QCOMPARE(b.previewLyric(7000),QString("Second line"));QCOMPARE(b.previewLyric(12000),QString());
    b.m_lyricOffsets["preview0001"]=1000;QCOMPARE(b.previewLyric(6000),QString("Second line"));
    b.m_playlists.clear();const auto id=b.createPlaylist("Mosaic");QVariantList songs;
    for(int i=0;i<6;++i){auto song=track(QString::number(i));song["art"]="file:///art-"+QString::number(i%4)+".png";songs.append(song);}b.addItemsToPlaylist(id,songs);
    QCOMPARE(b.playlists().first().toMap().value("artworks").toStringList().size(),4);b.m_playlists.clear();b.m_lyricOffsets.clear();
  }
  void smartPlaylistsAndDetails() {
    Backend b;b.m_playlists.clear();b.m_localTracks.clear();b.m_favorites.clear();b.m_lastPlayed.clear();
    auto a=track("smart000001"),c=track("smart000002"),d=track("smart000003");
    a["artist"]="Example Artist";c["artist"]="Different Artist";d["artist"]="Example Artist";
    a["source"]="local";a["localPath"]="/tmp/a.flac";c["source"]="local";c["localPath"]="/tmp/c.flac";d["source"]="local";
    d["id"]="local_smart000003";d["localPath"]="/tmp/missing-song.flac";
    b.m_localTracks={d};b.m_favorites={a};auto regular=b.createPlaylist("Saved");b.addItemsToPlaylist(regular,{a,c});
    const auto id=b.saveSmartPlaylist({},"Example",{{"artist","EXAMPLE"}});QVERIFY(!id.isEmpty());
    b.openPlaylist(id);QCOMPARE(b.results()->count(),2);
    b.addItemsToPlaylist(id,{c});b.removePlaylistRows(id,{0});b.movePlaylistTrack(id,0,1);QCOMPARE(b.results()->count(),2);
    b.saveSmartPlaylist(id,"Liked files",{{"source","local"},{"likedOnly",true}});QCOMPARE(b.results()->count(),1);
    b.toggleLike(c);QCOMPARE(b.results()->count(),2);b.toggleLike(a);QCOMPARE(b.results()->count(),1);
    b.m_lastPlayed[c.value("id").toString()]=QDateTime::currentSecsSinceEpoch()-qint64(40)*86400;
    b.saveSmartPlaylist(id,"Older",{{"days",30}});QCOMPARE(b.results()->count(),1);QCOMPARE(b.results()->get(0).value("id"),c.value("id"));
    b.saveSmartPlaylist(id,"Never",{{"days",-1}});QCOMPARE(b.results()->count(),2);
    b.save();b.load();b.openPlaylist(id);QCOMPARE(b.results()->count(),2);QVERIFY(b.smartPlaylist(id).contains("rules"));
    const auto exported=QUrl::fromLocalFile(storage.filePath("smart-export.json"));b.exportLibrary(exported);
    b.m_playlists.clear();b.importLibrary(exported);QVERIFY(b.smartPlaylist(id).contains("rules"));b.openPlaylist(id);QCOMPARE(b.results()->count(),2);
    b.deletePlaylist(id);QVERIFY(b.smartPlaylist(id).isEmpty());b.undo();QVERIFY(!b.smartPlaylist(id).isEmpty());
    auto details=b.trackDetails(d);QVERIFY(!details.isEmpty());
    bool missing=false;for(const auto &v:details)if(v.toMap().value("value")=="File missing")missing=true;QVERIFY(missing);
    a["streamUrl"]="https://private.invalid/?token=secret";for(const auto &v:b.trackDetails(a))QVERIFY(!v.toMap().value("value").toString().contains("secret"));
    Entries entries;entries.assign({a,d,c});QCOMPARE(entries.data(entries.index(0),Qt::UserRole+1).toString(),QString("Local files"));QCOMPARE(entries.data(entries.index(1),Qt::UserRole+1).toString(),QString("Local files"));QCOMPARE(entries.count(),3);
    b.m_playlists.clear();b.m_localTracks.clear();b.m_favorites.clear();b.m_lastPlayed.clear();
    const auto temporary=b.saveSmartPlaylist({},"Temporary",{});b.openPlaylist(temporary);b.undo();QCOMPARE(b.page(),QString("library"));QCOMPARE(b.libraryId(),QString("local-playlists"));
  }
  void viewOptionsAndDuplicateAdditions() {
    Backend b;b.m_playlists.clear();b.m_favorites={track("qol00000001"),track("qol00000002")};
    const auto id=b.createPlaylist("QOL fixture");
    const QVariantList batch{track("qol00000001"),track("qol00000002"),track("qol00000002")};
    auto info=b.playlistAdditionInfo(id,batch);QCOMPARE(info.value("added").toInt(),2);QCOMPARE(info.value("duplicates").toInt(),1);
    b.addToPlaylist(id,track("qol00000001"));
    info=b.playlistAdditionInfo(id,batch);QCOMPARE(info.value("added").toInt(),1);QCOMPARE(info.value("duplicates").toInt(),2);
    b.addItemsToPlaylist(id,batch);b.openPlaylist(id);QCOMPARE(b.results()->count(),2);QVERIFY(b.undoMessage().contains("skipped 2"));
    b.undo();QCOMPARE(b.results()->count(),1);
    b.collection()->setQuery("Track");b.collection()->setSortKey("artist");
    b.library("favorites");QCOMPARE(b.collection()->query(),QString());QCOMPARE(b.collection()->sortKey(),QString("original"));
    b.collection()->setSortKey("title");b.openPlaylist(id);
    QCOMPARE(b.collection()->query(),QString("Track"));QCOMPARE(b.collection()->sortKey(),QString("artist"));
    b.back();QCOMPARE(b.collection()->sortKey(),QString("title"));
    b.library("history");b.library("favorites");QCOMPARE(b.collection()->sortKey(),QString("title"));
    for(int i=0;i<40;++i)b.beginView(QString("bounded:%1").arg(i));QVERIFY(b.m_viewOptions.size()<=32);
    b.m_playlists.clear();b.m_favorites.clear();
  }
  void folderImportAndPlaylistCleanup() {
    const auto oldHelper=qgetenv("SUNG_HELPER"),oldPython=qgetenv("SUNG_PYTHON");
    const auto helper=QFileInfo(QString::fromUtf8(qgetenv("SUNG_FIXTURE_HELPER"))).dir().absoluteFilePath("../helper/catalog.py");
    qputenv("SUNG_HELPER",helper.toUtf8());qputenv("SUNG_PYTHON","/usr/bin/python3");
    const auto restore=qScopeGuard([&]{qputenv("SUNG_HELPER",oldHelper);qputenv("SUNG_PYTHON",oldPython);});
    QTemporaryDir music;QDir().mkpath(music.filePath("Album"));
    const auto path=music.filePath("Album/One.wav");
    QProcess encode;encode.start("ffmpeg",{"-nostdin","-v","error","-f","lavfi","-i","anullsrc=r=8000:cl=mono","-t","12",path});QVERIFY(encode.waitForFinished(10000));QCOMPARE(encode.exitCode(),0);
    Backend b;b.m_localTracks.clear();b.m_musicFolders.clear();b.m_playlists.clear();b.clearQueue();b.setVolume(0);b.setAutoplay(false);b.setPrepareNext(false);
    QVERIFY(!b.importMusicFolderPath("").isEmpty());
    QVERIFY(!b.importMusicFolderPath("relative/Music").isEmpty());
    QVERIFY(!b.importMusicFolderPath("smb://example.invalid/Music").isEmpty());
    QVERIFY(!b.importMusicFolderPath("file://example.invalid/Music").isEmpty());
    QVERIFY(!b.importMusicFolderPath(path).isEmpty());
    QVERIFY(!b.importMusicFolderPath(music.filePath("missing")).isEmpty());
    QVERIFY(b.musicFolders().isEmpty());QVERIFY(!b.importingLocal());
    QCOMPARE(b.importMusicFolderPath(music.path()),QString());QTRY_VERIFY_WITH_TIMEOUT(!b.importingLocal(),10000);QCOMPARE(b.m_localTracks.size(),1);QCOMPARE(b.musicFolders().size(),1);
    QCOMPARE(b.importMusicFolderPath(QUrl::fromLocalFile(music.path()).toString(QUrl::FullyEncoded)),QString());QTRY_VERIFY(!b.importingLocal());QCOMPARE(b.musicFolders().size(),1);QCOMPARE(b.m_localTracks.size(),1);
    const auto special=music.filePath("Space # and % and ü");QVERIFY(QDir().mkpath(special));
    QCOMPARE(b.importMusicFolderPath(special),QString());QTRY_VERIFY(!b.importingLocal());QVERIFY(b.musicFolders().contains(special));
    QCOMPARE(b.importMusicFolderPath(QUrl::fromLocalFile(special).toString(QUrl::FullyEncoded)),QString());QTRY_VERIFY(!b.importingLocal());QCOMPARE(b.musicFolders().size(),2);b.forgetMusicFolder(special);
    const auto first=b.m_localTracks.first().toMap();QVERIFY(!first.value("localStamp").toString().isEmpty());
    b.playItem(first);QTRY_VERIFY(b.playing());const auto source=b.media()->source();
    b.rescanMusicFolders();QTRY_VERIFY(!b.importingLocal());QCOMPARE(b.m_importDone,1);QCOMPARE(b.m_localTracks.size(),1);QVERIFY(b.playing());QCOMPARE(b.media()->source(),source);
    const auto secondPath=music.filePath("Two.wav");QVERIFY(QFile::copy(path,secondPath));b.rescanMusicFolders();QTRY_VERIFY(!b.importingLocal());QCOMPARE(b.m_localTracks.size(),2);
    b.save();b.load();QCOMPARE(b.musicFolders(),QStringList{music.path()});
    auto missing=b.m_localTracks.last().toMap();QVERIFY(QFile::remove(secondPath));b.rescanMusicFolders();QTRY_VERIFY(!b.importingLocal());QCOMPARE(b.m_localTracks.size(),2);
    auto qqTrack=track("abcdefghijk");const auto id=b.createPlaylist("Cleanup fixture");
    const QVariantList original{first,qqTrack,first,missing,qqTrack};
    b.m_playlists={QVariantMap{{"id",id},{"title","Cleanup fixture"},{"tracks",original}}};b.openPlaylist(id);
    b.inspectPlaylist(id);QTRY_VERIFY(!b.cleanupBusy());QCOMPARE(b.cleanupItems().size(),3);
    b.applyPlaylistCleanup(true,false);QTRY_VERIFY(!b.cleanupBusy());QCOMPARE(b.results()->count(),3);QVERIFY(b.playing());QCOMPARE(b.media()->source(),source);QVERIFY(QFile::exists(path));
    b.undo();QCOMPARE(b.results()->count(),5);
    b.inspectPlaylist(id);QTRY_VERIFY(!b.cleanupBusy());
    // A missing file restored after preview must survive the commit recheck.
    QVERIFY(QFile::copy(path,secondPath));b.applyPlaylistCleanup(false,true);QTRY_VERIFY(!b.cleanupBusy());QCOMPARE(b.results()->count(),5);
    QVERIFY(QFile::remove(secondPath));b.inspectPlaylist(id);QTRY_VERIFY(!b.cleanupBusy());b.applyPlaylistCleanup(true,true);QTRY_VERIFY(!b.cleanupBusy());QCOMPARE(b.results()->count(),2);b.undo();QCOMPARE(b.results()->count(),5);
    // Never apply stale row indices after an edit or reorder.
    b.inspectPlaylist(id);QTRY_VERIFY(!b.cleanupBusy());b.removePlaylistRows(id,{0});b.applyPlaylistCleanup(true,true);QTRY_VERIFY(!b.cleanupBusy());QCOMPARE(b.results()->count(),4);
    b.closePlaylistCleanup();QVERIFY(b.cleanupItems().isEmpty());
    b.rescanMusicFolders();b.cancelLocalImport();QTest::qWait(100);QVERIFY(!b.importingLocal());
    b.forgetMusicFolder(music.path());QVERIFY(b.musicFolders().isEmpty());QCOMPARE(b.m_localTracks.size(),2);QVERIFY(QFile::exists(path));
    b.stop();b.m_localTracks.clear();b.m_playlists.clear();b.clearQueue();b.setPrepareNext(true);b.setAutoplay(true);
  }
  void localAudioAndLyricSearch() {
    const auto oldHelper=qgetenv("SUNG_HELPER"),oldPython=qgetenv("SUNG_PYTHON");
    const auto helper=QFileInfo(QString::fromUtf8(qgetenv("SUNG_FIXTURE_HELPER"))).dir().absoluteFilePath("../helper/catalog.py");
    qputenv("SUNG_HELPER",helper.toUtf8());qputenv("SUNG_PYTHON","/usr/bin/python3");
    const auto restore=qScopeGuard([&]{qputenv("SUNG_HELPER",oldHelper);qputenv("SUNG_PYTHON",oldPython);});
    QTemporaryDir music;
    for(const auto &name:{"First song.flac","Second song.mp3"}){
      QProcess encode;encode.start("ffmpeg",{"-nostdin","-v","error","-f","lavfi","-i","anullsrc=r=44100:cl=mono","-t","12","-metadata","title=Local melody","-metadata","artist=Fixture artist","-metadata","album=Fixture album",music.filePath(name)});
      QVERIFY(encode.waitForFinished(10000));QCOMPARE(encode.exitCode(),0);
    }
    QFile lyrics(music.filePath("First song.lrc"));QVERIFY(lyrics.open(QIODevice::WriteOnly));lyrics.write("[00:01] A quiet morning\n[00:03] Another line\n[00:05] A QUIET evening");lyrics.close();
    Backend b;b.setVolume(0);b.clearQueue();b.setAutoplay(false);
    const QVariantList urls{QUrl::fromLocalFile(music.filePath("First song.flac")),QUrl::fromLocalFile(music.filePath("Second song.mp3"))};
    b.importLocalFiles(urls);QTRY_VERIFY_WITH_TIMEOUT(!b.importingLocal(),15000);b.library("files");QCOMPARE(b.results()->count(),2);
    auto first=b.results()->get(0),second=b.results()->get(1);QVERIFY(first.value("videoId").toString().isEmpty());QCOMPARE(first.value("artist").toString(),"Fixture artist");QCOMPARE(first.value("seconds").toInt(),12);
    b.importLocalFiles(urls);QTRY_VERIFY_WITH_TIMEOUT(!b.importingLocal(),10000);QCOMPARE(b.results()->count(),2);
    RowSelection selection;selection.setModel(b.results());selection.selectAll();QCOMPARE(selection.count(),2);
    const auto playlist=b.createPlaylist("Mixed sources");auto qqTrack=track("abcdefghijk");b.addItemsToPlaylist(playlist,{first,qqTrack,second});b.openPlaylist(playlist);QCOMPARE(b.results()->count(),3);
    b.playCollection(0);QTRY_VERIFY_WITH_TIMEOUT(b.playing()&&b.duration()>0,5000);QCOMPARE(b.media()->source(),urls[0].toUrl());QVERIFY(!b.m_processes.contains("play"));
    b.fetchLyrics();QCOMPARE(b.lyricsSource(),"Local LRC");auto matches=b.searchLyrics("quiet");QCOMPARE(matches.size(),2);QCOMPARE(matches.last().toMap().value("start").toLongLong(),5000);QVERIFY(b.searchLyrics("missing").isEmpty());
    b.setLyricOffset(250);b.seekLyric(matches.last().toMap().value("start").toLongLong());QCOMPARE(b.position(),4750);
    b.playKeepingQueue(second);QTRY_VERIFY(b.playing()&&b.media()->source()==urls[1].toUrl());QCOMPARE(b.queue()->count(),3);QCOMPARE(b.current().value("id"),second.value("id"));
    b.moveQueueRows({0},3);b.undo();QVERIFY(b.playing());b.pause();b.save();b.load();QCOMPARE(b.m_localTracks.size(),2);
    QFile exported(music.filePath("library.json"));b.exportLibrary(QUrl::fromLocalFile(exported.fileName()));QVERIFY(exported.open(QIODevice::ReadOnly));QVERIFY(exported.readAll().contains("localTracks"));
    b.stop();QVERIFY(QFile::rename(music.filePath("First song.flac"),music.filePath("Moved.flac")));b.playItem(first);QVERIFY(!b.playing());QVERIFY(b.error().contains("missing"));QVERIFY(!b.m_processes.contains("play"));
    b.locateLocalFile(QUrl::fromLocalFile(music.filePath("Moved.flac")),first.value("id").toString());QTRY_VERIFY_WITH_TIMEOUT(!b.importingLocal(),10000);QCOMPARE(b.current().value("id"),first.value("id"));QCOMPARE(b.current().value("localPath").toString(),music.filePath("Moved.flac"));
    b.retry();QTRY_VERIFY(b.playing());b.pause();b.openPlaylist(playlist);QCOMPARE(b.results()->get(0).value("localPath").toString(),music.filePath("Moved.flac"));
    b.importLocalFiles({QUrl::fromLocalFile(music.filePath("Moved.flac"))});QTRY_VERIFY(!b.importingLocal());QCOMPARE(b.m_localTracks.size(),2);
    b.removeLocalFile(first.value("id").toString());QVERIFY(QFile::exists(music.filePath("Moved.flac")));QCOMPARE(b.results()->count(),3);
    b.applyLyrics({{"ok",true},{"lyrics","Quiet text\nAnother quiet line"}});QCOMPARE(b.searchLyrics("quiet").size(),2);QCOMPARE(b.searchLyrics("quiet").first().toMap().value("start").toInt(),-1);
    b.importLocalFiles(urls);b.cancelLocalImport();QTest::qWait(150);QVERIFY(!b.importingLocal());
    b.stop();b.deletePlaylist(playlist);b.m_localTracks.clear();b.clearQueue();b.setAutoplay(true);
  }
  // The play log keeps 20,000 rows. The listening graph keeps every day's
  // total besides, so a heavy listener's earlier months stay on it, and a day
  // whose plays have gone still reports its time and count.
  void listeningGraphOutlivesThePlayLog() {
    Backend b;
    b.clearListeningStats();
    const QDate old = QDate::currentDate().addDays(-400);
    const qint64 at = QDateTime(old, QTime(9, 0)).toSecsSinceEpoch();
    b.m_plays.append(QVariantMap{{"at", at}, {"id", "early"}, {"title", "Early"}, {"artist", "Dawn"}, {"seconds", 240}});
    b.countListeningDay(at, 240);
    QCOMPARE(b.listeningStatsOn(old.toString(Qt::ISODate)).value("topSongs").toList().size(), 1);
    auto today = track("today0001");
    today["seconds"] = 180;
    for (int i = 0; i < 20000; ++i) b.recordPlay(today);
    QCOMPARE(b.m_plays.size(), 20000);
    QVERIFY(b.m_plays.first().toMap().value("id").toString() != "early");
    const auto day = b.listeningStatsOn(old.toString(Qt::ISODate));
    QCOMPARE(day.value("seconds").toLongLong(), 240);
    QCOMPARE(day.value("plays").toInt(), 1);
    QVERIFY(day.value("totalOnly").toBool());
    QVERIFY(day.value("topSongs").toList().isEmpty());
    const auto calendar = b.listeningCalendar(old.year());
    bool found = false;
    for (const auto &v : calendar.value("days").toList())
      if (v.toMap().value("date") == old.toString(Qt::ISODate))
        found = v.toMap().value("seconds").toLongLong() == 240 && v.toMap().value("level").toInt() > 0;
    QVERIFY(found);
    QVERIFY(calendar.value("years").toList().contains(old.year()));
    const auto now = b.listeningCalendar(0).value("days").toList().last().toMap();
    QCOMPARE(now.value("plays").toInt(), 20000);
    QCOMPARE(now.value("level").toInt(), 4);
    QVERIFY(b.libraryDocument().value("listeningDays").toMap().contains(old.toString(Qt::ISODate)));
    b.clearListeningStats();
    QVERIFY(b.m_listeningDays.isEmpty());
  }
  void listeningFeatures() {
    const auto oldHelper=qgetenv("SUNG_HELPER"),oldPython=qgetenv("SUNG_PYTHON");
    qputenv("SUNG_HELPER",qgetenv("SUNG_FIXTURE_HELPER"));qputenv("SUNG_PYTHON","/usr/bin/python3");qputenv("SUNG_BUFFER_FIXTURE","1");
    const auto restore=qScopeGuard([&]{qputenv("SUNG_HELPER",oldHelper);qputenv("SUNG_PYTHON",oldPython);qunsetenv("SUNG_BUFFER_FIXTURE");});
    auto parsed=Lrc::parse("[offset:100]\n[00:02.12][00:01.500] Test\n[00:01.500] Translation\n[00:04] End",6000);
    QCOMPARE(parsed.size(),3);QCOMPARE(parsed[0].toMap().value("start").toLongLong(),1400);QCOMPARE(parsed[0].toMap().value("text").toString(),"Test\nTranslation");QCOMPARE(parsed[1].toMap().value("start").toLongLong(),2020);
    QVERIFY(Lrc::parse("[ar:Artist] No timing").isEmpty());QVERIFY(Lrc::parse("[00:77] Invalid").isEmpty());QVERIFY(Lrc::parse(QString(262145,'x')).isEmpty());
    Backend b;b.clearQueue();b.setShuffle(false);b.setRepeat(0);b.setAutoplay(false);b.setPrepareNext(true);
    auto a=track("prepare0001"),next=track("prepare0002"),slow=track("prepare0003"),fail=track("prepare0004");
    a["id"]="qq_"+QString(64,'a');
    for(auto t:{a,next,slow,fail})b.enqueue(t);
    b.playAt(0);QTRY_VERIFY_WITH_TIMEOUT(b.playing()&&!b.resolving(),10000);
    const auto source=b.media()->source();QVERIFY(source.isLocalFile());const auto token=b.trackToken();
    QVERIFY(!b.m_prepareTimer.isActive()||b.m_preparedData.isEmpty());
    b.seek(20000);QTRY_VERIFY_WITH_TIMEOUT(!b.m_preparedData.isEmpty(),5000);
    QVERIFY(QFileInfo::exists(source.toLocalFile()));QCOMPARE(b.trackToken(),token);QCOMPARE(b.media()->source(),source);
    const auto prepared=b.m_preparedData.value("file").toString();QVERIFY(QFileInfo::exists(prepared));
    const auto preparedBytes=QFileInfo(prepared).size();
    // The head start is kept rather than thrown away when the track ends, so
    // the song plays out of the store. What this case is about is that it is
    // the buffer that was prepared and that nothing went back for it.
    b.next();QTRY_VERIFY_WITH_TIMEOUT(b.playing()&&!b.resolving(),5000);
    const auto playing=b.media()->source();
    QVERIFY2(playing.isLocalFile()&&QFileInfo::exists(playing.toLocalFile()),"the prepared song plays from a file");
    QCOMPARE(QFileInfo(playing.toLocalFile()).size(),preparedBytes);
    QVERIFY2(playing!=QUrl::fromLocalFile(prepared),"the buffer left the scratch directory for the store");
    // A reordered queue must discard the old target, without touching the decoder.
    b.seek(20000);QTRY_VERIFY_WITH_TIMEOUT(b.m_processes.contains("prepare"),3000);b.moveQueue(3,2);
    QTRY_COMPARE_WITH_TIMEOUT(b.m_preparedId,QString("prepare0004"),3000);QTest::qWait(600);QVERIFY(b.m_preparedData.isEmpty());QVERIFY(b.error().isEmpty());
    QCOMPARE(b.media()->source(),playing);QVERIFY(QFileInfo::exists(playing.toLocalFile()));
    b.moveQueue(3,2);QTRY_VERIFY_WITH_TIMEOUT(!b.m_preparedData.isEmpty(),5000);b.pause();QTRY_VERIFY(b.m_preparedId.isEmpty());QVERIFY(b.m_preparedData.isEmpty());
    b.play();b.setShuffle(true);QTest::qWait(100);QVERIFY(b.m_preparedId.isEmpty());b.setShuffle(false);b.setSleep(-1);QTest::qWait(100);QVERIFY(b.m_preparedId.isEmpty());b.setSleep(0);
    b.setPrepareNext(false);QTest::qWait(100);QVERIFY(b.m_preparedId.isEmpty());
    // Import is associated with the song captured when the picker opened.
    QTemporaryDir files;QFile lrc(files.filePath("lyrics.lrc"));QVERIFY(lrc.open(QIODevice::WriteOnly));lrc.write("[00:01.00] First\n[00:03.00] Second");lrc.close();
    b.importLyrics(QUrl::fromLocalFile(lrc.fileName()),a.value("id").toString());QVERIFY(b.lyricsSource()!="Imported LRC");
    b.playAt(0);b.fetchLyrics();QCOMPARE(b.lyricsSource(),"Imported LRC");QCOMPARE(b.lyricLines().size(),2);QTRY_VERIFY(b.playing()&&!b.resolving());b.seek(1500);QCOMPARE(b.lyricIndex(),0);
    b.resetLyrics();QTRY_VERIFY(!b.lyricsBusy());QCOMPARE(b.lyricsSource(),"QQ Music");
    // Saved tracks deduplicate across mixes and track plays separately from list order.
    b.m_favorites={a,next,slow};b.m_playlists={QVariantMap{{"id","mix-test"},{"tracks",QVariantList{a,fail}}}};
    b.m_lastPlayed={{a.value("id").toString(),QDateTime::currentSecsSinceEpoch()-31*86400},{next.value("id").toString(),QDateTime::currentSecsSinceEpoch()}};
    b.library("mixes");QCOMPARE(b.results()->count(),3);b.open(b.results()->get(0));QCOMPARE(b.title(),"Recently liked");QCOMPARE(b.results()->count(),3);
    b.library("mix-unplayed");QCOMPARE(b.results()->count(),2);b.library("mix-rediscover");QCOMPARE(b.results()->count(),1);QCOMPARE(b.results()->get(0).value("id"),a.value("id"));
    b.clearHistory();QVERIFY(b.m_lastPlayed.isEmpty());b.undo();QVERIFY(!b.m_lastPlayed.isEmpty());
    b.setHistoryPaused(true);b.playAt(2);QTRY_VERIFY(b.playing()&&!b.resolving());QVERIFY(!b.m_lastPlayed.contains(slow.value("id").toString()));b.setHistoryPaused(false);
    b.stop();b.m_favorites.clear();b.m_playlists.clear();b.m_lastPlayed.clear();b.clearQueue();b.setPrepareNext(true);b.setAutoplay(true);
  }
  void searchAndBulkOperations() {
    Backend b;b.clearQueue();for(const auto &q:b.recentSearches())b.removeRecentSearch(q);
    b.rememberSearch("  Blue   sky ");b.rememberSearch("blue sky");QCOMPARE(b.recentSearches().size(),1);
    b.setHistoryPaused(true);b.rememberSearch("private");QCOMPARE(b.recentSearches().size(),1);b.setHistoryPaused(false);
    b.rememberSearch("https://y.qq.com/n/ryqq/songDetail/secret");QCOMPARE(b.recentSearches().size(),1);
    for(int i=0;i<20;++i)b.rememberSearch(QString::number(i));QCOMPARE(b.recentSearches().size(),12);
    b.removeRecentSearch("19");QCOMPARE(b.recentSearches().first(),"18");
    QVariantList songs;for(int i=0;i<6;++i){auto t=track(QString("bulk%1").arg(i,7,10,QChar('0')));t["title"]=QString("Nebula %1").arg(i);t["seconds"]=120;songs.append(t);}
    const auto queuedSongs=Backend::queueWithOrigin(songs,"manual");
    const auto id=b.createPlaylist("Nebula collection");b.addItemsToPlaylist(id,songs);b.addItemsToPlaylist(id,songs);b.openPlaylist(id);QCOMPARE(b.results()->count(),6);
    b.enqueueItems(songs);b.playAt(2);b.pause();b.seek(23000);const auto current=b.current();const auto token=b.trackToken();
    b.moveQueueRows({0,2,2,-1,999},6);QCOMPARE(b.currentIndex(),5);QCOMPARE(b.current(),current);QCOMPARE(b.position(),23000);QCOMPARE(b.trackToken(),token);
    b.undo();QCOMPARE(b.queue()->rows,queuedSongs);QCOMPARE(b.currentIndex(),2);QCOMPARE(b.position(),23000);
    b.removeQueueRows({0,4});QCOMPARE(b.currentIndex(),1);QCOMPARE(b.current(),current);b.undo();QCOMPARE(b.currentIndex(),2);QCOMPARE(b.queue()->rows,queuedSongs);
    b.enqueueItems({songs[0],songs[1]},true);QCOMPARE(b.queue()->get(3),queuedSongs[0].toMap());QCOMPARE(b.queue()->get(4),queuedSongs[1].toMap());QCOMPARE(b.current(),current);b.undo();QCOMPARE(b.queue()->rows,queuedSongs);
    b.movePlaylistRows(id,{1,3},6);QCOMPARE(b.results()->get(4),songs[1].toMap());QCOMPARE(b.results()->get(5),songs[3].toMap());b.undo();QCOMPARE(b.results()->rows,songs);
    b.collection()->setSortKey("title");b.movePlaylistRows(id,{0},5);QCOMPARE(b.results()->rows,songs);
    b.collection()->setQuery("Nebula 4");const int source=b.collection()->sourceIndex(0);b.removePlaylistRows(id,{source});QCOMPARE(b.results()->count(),5);b.undo();QCOMPARE(b.results()->rows,songs);
    b.collection()->setQuery("");b.collection()->setSortKey("original");
    auto matches=b.localMatches("nebula");QVERIFY(matches.size()<=8);QCOMPARE(matches.first().toMap().value("kind").toString(),"local");
    auto exact=b.localMatches("nebula 2");QCOMPARE(exact.size(),1);QCOMPARE(exact.first().toMap().value("queueIndex").toInt(),2);
    QVERIFY(b.localMatches("zz-no-match").isEmpty());QVERIFY(b.localMatches("").isEmpty());
    b.removeQueueRows({0,1,2,3,4,5});QCOMPARE(b.currentIndex(),-1);QCOMPARE(b.queue()->count(),0);b.undo();QCOMPARE(b.queue()->rows,queuedSongs);
    b.deletePlaylist(id);b.clearQueue();
  }
  void selectionTracksModelChanges() {
    Entries entries;entries.assign({track("one"),track("two"),track("three"),QVariantMap{{"kind","album"}}});
    RowSelection selection;selection.setModel(&entries);selection.select(0);selection.select(2,Qt::ShiftModifier);QCOMPARE(selection.rows(),QVariantList({0,1,2}));
    selection.select(1,Qt::ControlModifier);QCOMPARE(selection.rows(),QVariantList({0,2}));QCOMPARE(selection.items().size(),2);
    selection.selectAll();QCOMPARE(selection.count(),3);entries.assign({track("new")});QCOMPARE(selection.count(),0);
    selection.select(0);entries.append({track("other")});QCOMPARE(selection.count(),0);
    CollectionView proxy;proxy.setSourceModel(&entries);selection.setModel(&proxy);selection.selectAll();proxy.setQuery("other");QCOMPARE(selection.count(),0);
    selection.select(0);QCOMPARE(selection.items().first().toMap().value("id").toString(),"other");
  }
  void homePinsAndPersistence() {
    const QVariantMap album{{"kind","album"},{"id","MPRE-test-pin"},{"title","Pinned album"}};
    QString id;
    {
      Backend b;for(const auto &v:b.pins())b.togglePin(v.toMap());
      b.togglePin(track("not-a-collection"));QVERIFY(b.pins().isEmpty());
      b.togglePin(album);QVERIFY(b.isPinned(album));QCOMPARE(b.pins().size(),1);
      id=b.createPlaylist("Pinned list");b.openPlaylist(id);b.togglePin(b.collectionItem());
      b.renamePlaylist(id,"Renamed pin");QCOMPARE(b.pins().first().toMap().value("title").toString(),"Renamed pin");
      b.deletePlaylist(id);QCOMPARE(b.pins().size(),1);
      b.undo();QCOMPARE(b.pins().size(),2);
      b.open(b.pins().first().toMap());QCOMPARE(b.page(),"local");QCOMPARE(b.libraryId(),id);
      b.save();
    }
    Backend restored;QVERIFY(restored.isPinned(album));QCOMPARE(restored.pins().size(),2);
    QTemporaryDir dir;const QUrl url=QUrl::fromLocalFile(dir.filePath("pins.json"));restored.exportLibrary(url);
    for(const auto &v:restored.pins())restored.togglePin(v.toMap());QVERIFY(restored.pins().isEmpty());
    restored.importLibrary(url);QCOMPARE(restored.pins().size(),2);
    restored.importLibrary(url);QCOMPARE(restored.pins().size(),2);
    for(const auto &v:restored.pins())restored.togglePin(v.toMap());restored.deletePlaylist(id);
  }
  void sleepAfterTrack() {
    Backend b;b.clearQueue();b.setSleep(-1);QCOMPARE(b.sleepLabel(),"Off");
    b.enqueue(track("sleep000001"));b.enqueue(track("sleep000002"));b.playAt(0);b.pause();
    b.setRepeat(2);b.setAutoplay(true);b.setSleep(-1);QCOMPARE(b.sleepLabel(),"End of track");
    b.media()->mediaStatusChanged(QMediaPlayer::EndOfMedia);
    QCOMPARE(b.sleepLabel(),"Off");QCOMPARE(b.currentIndex(),0);QVERIFY(!b.playing());QVERIFY(!b.resolving());
    b.setSleep(-1);b.playAt(1);QCOMPARE(b.sleepLabel(),"Off");b.pause();
    b.setSleep(-1);b.setSleep(15);QCOMPARE(b.sleepLabel(),"15 min");
    b.setSleep(-1);b.clearQueue();QCOMPARE(b.sleepLabel(),"Off");b.setRepeat(0);b.setAutoplay(false);
  }
  void playbackSpeedAndPitch() {
    {Backend b;b.setPlaybackRate(1.25);QCOMPARE(b.playbackRate(),1.25);
      b.setPlaybackRate(0);b.setPlaybackRate(3);b.setPlaybackRate(std::numeric_limits<double>::quiet_NaN());QCOMPARE(b.playbackRate(),1.25);
      if(b.pitchAdjustable()){b.setPreservePitch(false);QVERIFY(!b.preservePitch());b.setPreservePitch(true);QVERIFY(b.preservePitch());}
    }
    Backend restored;QCOMPARE(restored.playbackRate(),1.25);restored.setPlaybackRate(1);
  }
  void lyricTextSizePersistence() {
    {Backend b;b.setLyricTextSize(28);QCOMPARE(b.lyricTextSize(),28);}
    Backend b;QCOMPARE(b.lyricTextSize(),28);b.setLyricTextSize(2);QCOMPARE(b.lyricTextSize(),20);b.setLyricTextSize(100);QCOMPARE(b.lyricTextSize(),32);b.setLyricTextSize(25);
  }

  void listeningPreferences() {
    {Backend b;QVERIFY(!b.historyPaused());b.setHistoryPaused(true);b.setKeepCompletedLyrics(false);b.setVolumeStep(2);b.setVolumeStep(7);QCOMPARE(b.volumeStep(),2);}
    Backend b;QVERIFY(!b.historyPaused());QVERIFY(!b.keepCompletedLyrics());QCOMPARE(b.volumeStep(),2);b.setKeepCompletedLyrics(true);b.setVolumeStep(5);
  }
  void queueTimeAndSmartShuffle() {
    qputenv("SUNG_HELPER",qgetenv("SUNG_FIXTURE_HELPER"));qputenv("SUNG_PYTHON","python3");
    Backend b;b.clearQueue();b.setShuffle(false);b.setRepeat(0);b.setAutoplay(false);b.setPlaybackRate(1);
    for(int i=0;i<7;++i){auto t=track(QString("queue%1").arg(i,6,10,QChar('0')));t["seconds"]=120;t["artist"]=QString(QChar('A'+i%3));b.enqueue(t);}
    b.playAt(0);b.pause();QTRY_VERIFY_WITH_TIMEOUT(!b.resolving(),5000);b.seek(30000);
    QCOMPARE(b.queueRemainingMs(),810000);QCOMPARE(b.queueTime(),"14 min left");QVERIFY(b.queueEnd().isEmpty());
    b.setPlaybackRate(1.5);QCOMPARE(b.queueRemainingMs(),540000);b.setPlaybackRate(1);
    b.enqueue(track("unknown0001"));QCOMPARE(b.queueRemainingMs(),-1);b.removeQueue(7);QCOMPARE(b.queueRemainingMs(),810000);
    const auto original=b.queue()->rows;const auto current=b.current();b.setShuffle(true);QCOMPARE(b.queueRemainingMs(),-1);
    b.smartShuffleQueue();QVERIFY(!b.shuffle());QCOMPARE(b.current(),current);QCOMPARE(b.currentIndex(),0);QCOMPARE(b.position(),30000);
    auto ids=[](const QVariantList &rows){QStringList result;for(const auto &row:rows)result.append(row.toMap().value("id").toString());result.sort();return result;};
    QCOMPARE(ids(b.queue()->rows),ids(original));
    for(int i=1;i<b.queue()->count();++i)QVERIFY(b.queue()->get(i).value("artist")!=b.queue()->get(i-1).value("artist"));
    b.undo();QCOMPARE(b.queue()->rows,original);QVERIFY(b.shuffle());QCOMPARE(b.position(),30000);
    b.smartShuffleQueue();b.next();b.pause();QVERIFY(b.undoMessage().isEmpty());
    b.setRepeat(1);QVERIFY(b.queueTime().endsWith("queued"));QVERIFY(b.queueEnd().isEmpty());
    b.clearQueue();QCOMPARE(b.queueRemainingMs(),0);b.setRepeat(0);b.setShuffle(false);
    qunsetenv("SUNG_HELPER");qunsetenv("SUNG_PYTHON");
  }
  void lyricTimingAndPersistence() {
    qputenv("SUNG_HELPER",qgetenv("SUNG_FIXTURE_HELPER"));
    qputenv("SUNG_PYTHON","python3");
    {
      Backend b;b.clearQueue();auto timed=track("timedlyric1");timed["seconds"]=120;b.enqueue(timed);b.enqueue(track("otherlyrics"));b.playAt(0);b.pause();
      QTRY_VERIFY_WITH_TIMEOUT(!b.resolving(),5000);b.fetchLyrics();
      QTRY_VERIFY_WITH_TIMEOUT(!b.lyricsBusy(),5000);QCOMPARE(b.lyricLines().size(),2);
      b.setLyricOffset(0);b.seek(500);QCOMPARE(b.lyricIndex(),-1);
      b.setLyricOffset(750);QCOMPARE(b.lyricIndex(),0);
      b.seekLyric(3000);QCOMPARE(b.position(),2250);QCOMPARE(b.lyricIndex(),1);
      b.setLyricOffset(-500);b.seekLyric(1000);QCOMPARE(b.position(),1500);QCOMPARE(b.lyricIndex(),0);
      b.playAt(1);b.pause();QCOMPARE(b.lyricOffset(),0);b.playAt(0);b.pause();QCOMPARE(b.lyricOffset(),-500);b.save();
    }
    Backend restored;QCOMPARE(restored.lyricOffset(),-500);
    QTemporaryDir dir;const auto url=QUrl::fromLocalFile(dir.filePath("timing.json"));restored.exportLibrary(url);
    restored.setLyricOffset(0);restored.importLibrary(url);QCOMPARE(restored.lyricOffset(),-500);
    restored.setLyricOffset(99999);QCOMPARE(restored.lyricOffset(),10000);restored.setLyricOffset(0);restored.clearQueue();
    qunsetenv("SUNG_HELPER");qunsetenv("SUNG_PYTHON");
  }
  void immediatePlaybackKeepsQueue() {
    Backend b;b.clearQueue();
    for(const auto &id:{"keepqueue01","keepqueue02","keepqueue03"})b.enqueue(track(id));
    b.playAt(0);b.pause();b.playKeepingQueue(track("keepqueue04"));b.pause();
    QCOMPARE(b.queue()->count(),4);QCOMPARE(b.currentIndex(),1);
    QCOMPARE(b.queue()->get(0).value("id").toString(),"keepqueue01");QCOMPARE(b.queue()->get(2).value("id").toString(),"keepqueue02");
    b.playKeepingQueue(track("keepqueue03"));b.pause();QCOMPARE(b.currentIndex(),2);QCOMPARE(b.queue()->count(),4);
    QCOMPARE(b.queue()->get(3).value("id").toString(),"keepqueue02");
    b.playKeepingQueue(track("keepqueue03"));b.pause();QCOMPARE(b.currentIndex(),2);QCOMPARE(b.queue()->count(),4);
    b.playKeepingQueue({{"kind","album"},{"id","not a song"}});QCOMPARE(b.queue()->count(),4);
    b.clearQueue();b.playKeepingQueue(track("keepqueue01"));b.pause();QCOMPARE(b.queue()->count(),1);QCOMPARE(b.currentIndex(),0);b.clearQueue();
  }
  void queueAndValidation() {
    Backend b;
    b.clearQueue();
    b.enqueue({{"id", "album"}, {"kind", "album"}});
    QCOMPARE(b.queue()->count(), 0);
    b.enqueue(track("aaaaaaaaaaa"));
    b.enqueue(track("bbbbbbbbbbb"));
    b.enqueue(track("ccccccccccc"));
    QCOMPARE(b.queue()->count(), 3);
    b.moveQueue(2, 0);
    QCOMPARE(b.queue()->get(0).value("id").toString(), "ccccccccccc");
    b.removeQueue(-1);
    b.removeQueue(999);
    QCOMPARE(b.queue()->count(), 3);
    b.removeQueue(1);
    QCOMPARE(b.queue()->count(), 2);
    b.clearQueue();
    QCOMPARE(b.currentIndex(), -1);
  }
  void favoritesAndPersistence() {
    QString id;
    {
      Backend b;
      b.clearQueue();
      auto t = track("favorite001");t["source"]="local";t["localPath"]="/tmp/favorite001.wav";
      if (b.isLiked("favorite001"))
        b.toggleLike(t);
      b.toggleLike(t);
      QVERIFY(b.isLiked("favorite001"));
      id = b.createPlaylist("  Evening  ");
      QVERIFY(!id.isEmpty());
      b.addToPlaylist(id, t);
      b.addToPlaylist(id, t);
      b.openPlaylist(id);
      QCOMPARE(b.results()->count(), 1);
      QCOMPARE(b.title(), "Evening");
      b.renamePlaylist(id, "Night");
      QCOMPARE(b.title(), "Night");
      b.enqueue(t);
      b.save();
    }
    {
      Backend b;
      QVERIFY(b.isLiked("favorite001"));
      b.openPlaylist(id);
      QCOMPARE(b.results()->count(), 1);
      QCOMPARE(b.title(), "Night");
      QCOMPARE(b.queue()->count(), 1);
      b.removeFromPlaylist(id, 0);
      QCOMPARE(b.results()->count(), 0);
      b.deletePlaylist(id);
      auto t=track("favorite001");t["source"]="local";t["localPath"]="/tmp/favorite001.wav";b.toggleLike(t);
      QVERIFY(!b.isLiked("favorite001"));
    }
  }
  void settingsAndFormatting() {
    Backend b;
    b.setVolume(2);
    QCOMPARE(b.volume(), 1.0);
    b.setVolume(-1);
    QCOMPARE(b.volume(), 0.0);
    b.setRepeat(10);
    QCOMPARE(b.repeat(), 2);
    b.setTheme("light");
    b.setTheme("garbage");
    QCOMPARE(b.theme(), "light");
    b.setMotion(false);
    QVERIFY(!b.motion());
    QCOMPARE(b.formatTime(65000), "1:05");
    QCOMPARE(b.formatTime(3661000), "1:01:01");
    QCOMPARE(b.formatTime(-1), "0:00");
    b.setSleep(15);
    QCOMPARE(b.sleepLabel(), "15 min");
    b.setSleep(0);
    QCOMPARE(b.sleepLabel(), "Off");
  }
  void undoAndValidation() {
    Backend b;
    b.clearQueue();
    b.enqueue(track("aaaaaaaaaaa"));
    b.enqueue(track("bbbbbbbbbbb"));
    b.clearQueue();
    QCOMPARE(b.queue()->count(),0);
    b.undo();
    QCOMPARE(b.queue()->count(),2);
    QVERIFY(!b.playing());
    QVERIFY(b.createPlaylist("   ").isEmpty());
    auto id=b.createPlaylist("Recover me");
    b.addToPlaylist(id,track("aaaaaaaaaaa"));
    auto count=b.playlists().size();
    b.deletePlaylist(id);
    QCOMPARE(b.playlists().size(),count-1);
    b.undo();
    b.openPlaylist(id);
    QCOMPARE(b.results()->count(),1);
    b.copyLink(track("aaaaaaaaaaa"));
    QCOMPARE(QGuiApplication::clipboard()->text(),"https://y.qq.com/n/ryqq/songDetail/aaaaaaaaaaa");
    b.notifyError("ConnectionError: private details", "catalog");
    QVERIFY(b.canRetry());
    QVERIFY(!b.error().contains("private details"));
    b.dismissError();
    QVERIFY(!b.canRetry());
  }
  void catalogRequestsAndCancellation() {
    const auto helper=qgetenv("SUNG_FIXTURE_HELPER");
    QVERIFY2(!helper.isEmpty(), "CTest must provide the transport fixture path");
    qputenv("SUNG_HELPER",helper);
    qputenv("SUNG_PYTHON","python3");
    Backend b;
    b.home();
    QTRY_VERIFY_WITH_TIMEOUT(!b.busy(),5000);
    QCOMPARE(b.sections().size(),1);

    b.search("slow");
    b.search("latest","songs");
    QTRY_VERIFY_WITH_TIMEOUT(!b.busy(),5000);
    QCOMPARE(b.query(),"latest");
    QCOMPARE(b.results()->count(),30);
    QVERIFY(b.canMore());
    b.more();
    QTRY_VERIFY_WITH_TIMEOUT(!b.busy(),5000);
    QCOMPARE(b.results()->count(),60);
    b.open({{"kind","album"},{"source","qqmusic"},{"browseId","ALBUM"},{"title","Album"}});
    QTRY_VERIFY_WITH_TIMEOUT(!b.busy(),5000);
    QCOMPARE(b.results()->count(),2);
    b.back();
    QCOMPARE(b.query(),"latest");
    QCOMPARE(b.results()->count(),60);
    b.search("error");
    QTRY_VERIFY_WITH_TIMEOUT(!b.busy(),5000);
    QVERIFY(b.canRetry());
    b.retry();
    QTRY_VERIFY_WITH_TIMEOUT(!b.busy(),5000);
    QVERIFY(b.canRetry());
    b.search("empty");
    QTRY_VERIFY_WITH_TIMEOUT(!b.busy(),5000);
    QVERIFY(b.error().isEmpty());
    QCOMPARE(b.results()->count(),0);
    b.openLink("https://y.qq.com/n/ryqq/songDetail/00000000001");
    QTRY_VERIFY_WITH_TIMEOUT(!b.busy(),5000);
    QCOMPARE(b.results()->count(),2);
    b.clearQueue();
    b.enqueueResults();
    QCOMPARE(b.queue()->count(),2);
    b.setAutoplay(false);
    b.playAt(0);
    QTRY_VERIFY_WITH_TIMEOUT(!b.resolving(),5000);
    QCOMPARE(b.currentIndex(),0);
    b.next();
    QCOMPARE(b.currentIndex(),1);
    b.previous();
    QCOMPARE(b.currentIndex(),0);
    b.setShuffle(true);
    b.next();
    QCOMPARE(b.currentIndex(),1);
    b.setShuffle(false);
    b.setRepeat(1);
    b.next();
    QCOMPARE(b.currentIndex(),0);
    b.fetchLyrics();
    QTRY_VERIFY_WITH_TIMEOUT(!b.lyricsBusy(),5000);
    QCOMPARE(b.lyrics(),"Test lyrics");
    b.radio(b.results()->get(0));
    QTRY_COMPARE_WITH_TIMEOUT(b.queue()->count(),4,5000);
    b.stop();
    qunsetenv("SUNG_HELPER");
    qunsetenv("SUNG_PYTHON");
  }
  void libraryExportMergeAndUndo() {
    Backend b;b.clearQueue();
    b.enqueue(track("aaaaaaaaaaa"));b.enqueue(track("bbbbbbbbbbb"));
    b.removeQueue(0);b.enqueue(track("ccccccccccc"));b.undo();
    QCOMPARE(b.queue()->count(),2);
    QCOMPARE(b.queue()->get(0).value("id").toString(),"bbbbbbbbbbb");
    b.saveQueue("Saved queue");
    const auto id=b.playlists().last().toMap().value("id").toString();
    b.movePlaylistTrack(id,1,0);b.openPlaylist(id);
    QCOMPARE(b.results()->get(0).value("id").toString(),"ccccccccccc");
    QTemporaryDir temp;const auto url=QUrl::fromLocalFile(temp.filePath("library.json"));
    b.exportLibrary(url);QVERIFY(QFile::exists(url.toLocalFile()));
    const auto count=b.playlists().size();b.deletePlaylist(id);
    b.importLibrary(url);QCOMPARE(b.playlists().size(),count);
    b.importLibrary(url);QCOMPARE(b.playlists().size(),count);
    b.openPlaylist(id);QCOMPARE(b.results()->count(),2);
    b.deletePlaylist(id);b.clearQueue();
  }
  void timedLyricsFollowSeekAndClear() {
    qputenv("SUNG_HELPER",qgetenv("SUNG_FIXTURE_HELPER"));qputenv("SUNG_PYTHON","python3");
    Backend b;auto t=track("timedlyric1");t["seconds"]=120;b.playItem(t);
    QTRY_VERIFY_WITH_TIMEOUT(!b.resolving(),5000);b.fetchLyrics();
    QTRY_VERIFY_WITH_TIMEOUT(!b.lyricsBusy(),5000);QCOMPARE(b.lyricLines().size(),2);
    b.seek(500);QCOMPARE(b.lyricIndex(),-1);
    QSignalSpy changes(&b,&Backend::lyricIndexChanged);
    b.seek(1500);QCOMPARE(b.lyricIndex(),0);QCOMPARE(changes.count(),1);
    b.seek(1600);b.seek(1700);QCOMPARE(changes.count(),1);
    b.setLyricOffset(1000);QCOMPARE(b.lyricIndex(),-1);QCOMPARE(changes.count(),2);
    b.setLyricOffset(0);QCOMPARE(b.lyricIndex(),0);QCOMPARE(changes.count(),3);
    b.seek(2500);QCOMPARE(b.lyricIndex(),-1);
    b.seek(3500);QCOMPARE(b.lyricIndex(),1);
    b.seek(5000);QCOMPARE(b.lyricIndex(),-1);
    b.stop();QVERIFY(b.lyricLines().isEmpty());QCOMPARE(b.lyricIndex(),-1);
    qunsetenv("SUNG_HELPER");qunsetenv("SUNG_PYTHON");
  }
  void savedPlaybackPosition() {
    qputenv("SUNG_HELPER",qgetenv("SUNG_FIXTURE_HELPER"));qputenv("SUNG_PYTHON","python3");
    {
      Backend b;b.clearQueue();auto t=track("position001");t["seconds"]=180;
      b.playItem(t);QTRY_VERIFY_WITH_TIMEOUT(!b.resolving(),5000);
      b.seek(60000);b.save();QCOMPARE(b.position(),qint64(60000));
    }
    {
      Backend b;QCOMPARE(b.position(),qint64(60000));QVERIFY(!b.playing());
      b.stop();QCOMPARE(b.position(),qint64(0));QVERIFY(b.stopped());b.clearQueue();
    }
    qunsetenv("SUNG_HELPER");qunsetenv("SUNG_PYTHON");
  }

  void noctaliaUpdatesAtomically() {
    QTemporaryDir files;
    const auto path=files.filePath("noctalia.colors");
    qputenv("SUNG_NOCTALIA_COLORS",path.toUtf8());
    auto write=[&](bool light){
      QSaveFile file(path); QVERIFY(file.open(QIODevice::WriteOnly));
      QByteArray data;
      const QList<QByteArray> groups={"View","Window","Button","Complementary"};
      const QList<QByteArray> keys={"BackgroundNormal","BackgroundAlternate","ForegroundNormal","ForegroundInactive","ForegroundActive","ForegroundLink","DecorationHover","DecorationFocus"};
      for(const auto &g:groups){data+="[Colors:"+g+"]\n";for(const auto &k:keys)data+=k+"="+(light?"240,240,240":"20,20,20")+"\n";}
      file.write(data); QVERIFY(file.commit());
    };
    write(false);
    DesktopTheme t;
    QVERIFY(t.available()); QVERIFY(t.dark());
    QSignalSpy changed(&t,&DesktopTheme::changed);
    write(true);
    QTRY_VERIFY_WITH_TIMEOUT(!t.dark(),3000);
    QVERIFY(changed.count()>0);
    QFile f(path); QVERIFY(f.open(QIODevice::WriteOnly));f.write("partial");f.close();
    QTest::qWait(200);
    QVERIFY(t.available()); QVERIFY(!t.dark());
    write(false);
    QTRY_VERIFY_WITH_TIMEOUT(t.dark(),3000);
    qunsetenv("SUNG_NOCTALIA_COLORS");
  }
  void collectionFilterSortAndPlayback() {
    qputenv("SUNG_HELPER",qgetenv("SUNG_FIXTURE_HELPER"));qputenv("SUNG_PYTHON","python3");
    Backend b;b.clearQueue();
    auto a=track("collection1");a["title"]="Echo 10";a["artist"]="Alpha";a["duration"]="4:05";
    auto c=track("collection2");c["title"]="echo 2";c["artist"]="Beta";c["duration"]="1:05:00";
    auto d=track("collection3");d["title"]="Other";d["artist"]="Alpha";d["seconds"]=80;
    b.results()->assign({a,c,d});auto view=b.collection();
    view->setQuery("ECHO beta");QCOMPARE(view->count(),1);QCOMPARE(view->sourceIndex(0),1);
    QCOMPARE(view->sourceIndex(9),-1);
    view->setQuery("Alpha");view->setSortKey("duration");
    QCOMPARE(view->count(),2);QCOMPARE(view->get(0).value("id"),d.value("id"));
    QCOMPARE(b.results()->get(0).value("id"),a.value("id"));
    b.playCollection(0);QCOMPARE(b.queue()->count(),2);QCOMPARE(b.current().value("id"),d.value("id"));b.stop();
    view->setQuery("");view->setSortKey("title");
    QCOMPARE(view->get(0).value("id"),c.value("id"));
    view->setSortKey("original");QCOMPARE(view->get(0).value("id"),a.value("id"));
    view->setQuery("no matches");QCOMPARE(view->count(),0);b.enqueueCollection();QCOMPARE(b.queue()->count(),2);
    b.library("favorites");QVERIFY(view->query().isEmpty());QCOMPARE(view->sortKey(),"original");
    b.back();QCOMPARE(view->query(),"no matches");b.clearQueue();
    qunsetenv("SUNG_HELPER");qunsetenv("SUNG_PYTHON");
  }
  void folderSortingPreservesIdentityAndSelection() {
    Entries entries;CollectionView view;view.setSourceModel(&entries);
    auto song=[](const QString &id,const QString &path){return QVariantMap{{"id",id},{"kind","song"},{"title","Identical title"},{"localPath",path}};};
    const QVariantList rows={song("ten","/Music/Artist/Album/10.wav"),song("two","/Music/Artist/Album/2.wav"),song("other","/Music/Other/Album/1.wav"),song("case","/Music/artist/Album/1.wav"),song("unicode","/Music/Été #100%/1.wav")};
    entries.assign(rows);view.setSortKey("folder");
    QCOMPARE(view.get(0).value("id").toString(),"two");QCOMPARE(view.get(1).value("id").toString(),"ten");
    QCOMPARE(view.sourceIndex(0),1);QCOMPARE(entries.rows,rows);
    const int role=Qt::UserRole+2;QCOMPARE(entries.roleNames().value(role),"musicFolder");
    QCOMPARE(view.data(view.index(0,0),role).toString(),"/Music/Artist/Album");
    QSet<QString> closed;QString previous;
    for(int i=0;i<view.count();++i){const auto folder=view.data(view.index(i,0),role).toString();if(folder!=previous){QVERIFY(!closed.contains(folder));closed.insert(folder);previous=folder;}}
    RowSelection selection;selection.setModel(&view);selection.select(0,0);selection.select(1,Qt::ControlModifier);
    QCOMPARE(selection.items().size(),2);QCOMPARE(selection.items()[0].toMap().value("id").toString(),"two");
    view.setQuery("Other Album");QCOMPARE(view.count(),1);QCOMPARE(view.sourceIndex(0),2);QCOMPARE(selection.count(),0);
    view.setQuery("Été #100%");QCOMPARE(view.count(),1);QCOMPARE(view.sourceIndex(0),4);
    view.setQuery("");view.setSortKey("original");QCOMPARE(view.items(),rows);
    view.setSortKey("invalid");QCOMPARE(view.sortKey(),"original");
  }
  void folderHeadingLabels() {
    Backend b;b.m_musicFolders={"/mnt/Music"};
    QCOMPARE(b.musicFolderLabel("/mnt/Music/Artist/Album"),"Music/Artist/Album");
    QCOMPARE(b.musicFolderLabel("/mnt/Music"),"Music");
    QCOMPARE(b.musicFolderLabel("/mnt/Music-other/Album"),"/mnt/Music-other/Album");
    b.m_musicFolders.append("/media/Music");
    QCOMPARE(b.musicFolderLabel("/mnt/Music/Album"),"/mnt/Music/Album");
    QCOMPARE(b.musicFolderLabel("/media/Music/Album"),"/media/Music/Album");
    b.m_musicFolders={"/mnt/Music","/mnt/Music/Artist"};
    QCOMPARE(b.musicFolderLabel("/mnt/Music/Artist/Album"),"Artist/Album");
    b.m_musicFolders.clear();QCOMPARE(b.musicFolderLabel(QDir::homePath()+"/Music/Album"),"~/Music/Album");
  }
  void animatedArtworkPreferencePersists() {
    {Backend b;b.setAnimatedArtwork(false);QVERIFY(!b.animatedArtwork());}
    {Backend b;QVERIFY(!b.animatedArtwork());b.setAnimatedArtwork(true);}
    {Backend b;QVERIFY(b.animatedArtwork());}
  }
  void audioOutputValidation() {
    Backend b;b.setAudioDeviceId("");
    QVERIFY(!b.audioDevices().isEmpty());QCOMPARE(b.audioDevices().first().toMap().value("name").toString(),"System default");
    b.setAudioDeviceId("missing-output");QVERIFY(b.audioDeviceId().isEmpty());
    QCOMPARE(b.audioDeviceName(),"System default");
  }
  void navigation() {
    Backend b;
    b.library("favorites");
    QCOMPARE(b.page(), "library");
    auto id = b.createPlaylist("Back test");
    b.openPlaylist(id);
    QCOMPARE(b.page(), "local");
    QVERIFY(b.canBack());
    b.back();
    QCOMPARE(b.page(), "library");
    b.deletePlaylist(id);
  }
};
QTEST_MAIN(BackendTest)
#include "backend_test.moc"
