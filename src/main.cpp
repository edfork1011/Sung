#include "backend.h"
#ifdef SUNG_DIAGNOSTICS
#include "qqmusic_ui.h"
#include "desktop_ui.h"
#endif
#include "rowselection.h"
#include "desktoptheme.h"
#include "freedmemory.h"
#include "mpris.h"
#include "desktopintegration.h"
#include "roundedart.h"
#include "motionbackdrop.h"
#include "windowresources.h"
#include <QDir>
#include <QCache>
#include <QMutex>
#include <QMutexLocker>
#include <QFile>
#include <QFontDatabase>
#include <QApplication>
#include <QIcon>
#include <QRegularExpression>
#include <QLocalServer>
#include <QLocalSocket>
#include <QNetworkAccessManager>
#include <QNetworkDiskCache>
#include <QPainter>
#include <QQmlApplicationEngine>
#include <QQmlComponent>
#include <QQmlContext>
#include <QQmlNetworkAccessManagerFactory>
#include <QQuickImageProvider>
#include <QQuickPaintedItem>
#include <QEventLoop>
#include <QAbstractAnimation>
#include <QtMath>
#include <QQuickStyle>
#include <QQuickWindow>
#include <QStandardPaths>
#include <QSvgRenderer>
#include <QTimer>
#include <unistd.h>
#include <cstdio>
#include <algorithm>
#include <functional>
#include <optional>

class Symbols : public QQuickImageProvider {
public:
  Symbols() : QQuickImageProvider(QQuickImageProvider::Image) {}
  QImage requestImage(const QString &id, QSize *size,
                      const QSize &requested) override {
    auto parts = id.split('/');
    QString name = parts.value(0);
    if (name.contains(".."))
      return {};
    QSize s = !requested.isEmpty() ? requested : QSize(24, 24);
    s = s.boundedTo(QSize(256, 256));
    // Material draws each symbol at several optical sizes rather than scaling
    // one of them, so a glyph keeps its stroke weight wherever it is used. The
    // caller says how many points it is asking for, because the pixels it wants
    // depend on the display and the optical size does not.
    const int points = parts.value(1).toInt();
    if (points > 0 && points <= 22) name += "_20";
    else if (points > 32) name += "_40";
    // Cache the untinted raster: every ink a symbol is drawn in is painted
    // from one SVG coverage mask, so a theme transition does not parse the
    // file again. The tinted picture is Qt's pixmap cache's to share.
    QMutexLocker lock(&m_mutex);
    const QString key=name+":"+QString::number(s.width())+"x"+QString::number(s.height());
    QImage img;
    if (const auto mask=m_masks.object(key)) img=*mask;
    else {
      QFile f(":/assets/icons/" + name + ".svg");
      // A symbol with no drawing at that optical size falls back to its own.
      if (!f.open(QIODevice::ReadOnly)) {
        f.setFileName(":/assets/icons/" + parts.value(0) + ".svg");
        if (!f.open(QIODevice::ReadOnly)) return {};
      }
      QSvgRenderer svg(f.readAll());
      img=QImage(s,QImage::Format_ARGB32_Premultiplied);
      img.fill(Qt::transparent);
      QPainter painter(&img);svg.render(&painter);painter.end();
      m_masks.insert(key,new QImage(img),int(img.sizeInBytes()));
    }
    QPainter p(&img);
    p.setCompositionMode(QPainter::CompositionMode_SourceIn);
    p.fillRect(img.rect(), QColor("#" + parts.value(2, "ffffff")));
    p.end();
    if (size)
      *size = s;
    return img;
  }
private:
  QCache<QString,QImage> m_masks{512*1024};
  QMutex m_mutex;
};
int main(int argc, char **argv) {
  if (!qEnvironmentVariableIsSet("QT_FFMPEG_DECODING_HW_DEVICE_TYPES"))
    qputenv("QT_FFMPEG_DECODING_HW_DEVICE_TYPES", ",");
  if (!qEnvironmentVariableIsSet("QT_FFMPEG_ENCODING_HW_DEVICE_TYPES"))
    qputenv("QT_FFMPEG_ENCODING_HW_DEVICE_TYPES", ",");
  // Qt Quick packs small images into one texture atlas per window, sized by
  // default to the window rounded up to a power of two. For the default window
  // on a 1.6x display that is 2048x2048, allocated whole with the first image,
  // and the NVIDIA driver keeps a copy in the process besides: 16 MiB of heap
  // and 15 MiB pinned, for symbols that filled a tenth of it. The symbols are
  // nearly all that goes in, and a tour of the interface at 1.6x peaks at
  // 0.80 Mpx of them, which 1024x1024 holds. An image that does not fit is
  // given a texture of its own, so a fuller atlas costs batching, not pixels.
  // https://doc.qt.io/qt-6/qtquick-visualcanvas-scenegraph-renderer.html#texture-atlas
  if (!qEnvironmentVariableIsSet("QSG_ATLAS_WIDTH"))
    qputenv("QSG_ATLAS_WIDTH", "1024");
  if (!qEnvironmentVariableIsSet("QSG_ATLAS_HEIGHT"))
    qputenv("QSG_ATLAS_HEIGHT", "1024");
  QApplication app(argc, argv);
  app.setWindowIcon(QIcon(":/assets/sung.png"));
  app.setApplicationName("sung");
  app.setApplicationDisplayName("Sung");
  app.setOrganizationName("Sung");
  app.setApplicationVersion("0.12.0");
  app.setDesktopFileName("sung");
  const auto args = app.arguments();
  if (args.contains("--version")) {
    fprintf(stdout, "Sung 0.12.0\n");
    return 0;
  }
  // A checkout instance shares the desktop bus, but owns its own activation
  // socket and MPRIS name so an installed Sung can coexist with it.
  QString instance;
  if (args.contains("--instance")) {
    instance=args.value(args.indexOf("--instance")+1);
    if (!QRegularExpression("^[A-Za-z_][A-Za-z0-9_]{0,63}$").match(instance).hasMatch()) {
      qCritical("--instance requires a name beginning with a letter or underscore");return 2;
    }
  }
  const auto socketName="sung-"+QString::number(getuid())+(instance.isEmpty()?QString():"-"+instance);
  QLocalSocket peer;
  if (!args.contains("--isolated")) {
    peer.connectToServer(socketName);
    if (peer.waitForConnected(120)) {
      QByteArray command=args.contains("--mini")?QByteArray("--mini"):QByteArray("raise");
      for (const auto &arg:args) if(arg.startsWith("https://")) {command=arg.toUtf8();break;}
      peer.write(command);peer.flush();peer.waitForBytesWritten(150);return 0;
    }
  }
  QLocalServer server;
  if (!args.contains("--isolated")) {
    QLocalServer::removeServer(socketName);
    server.setSocketOptions(QLocalServer::UserAccessOption);
    if (!server.listen(socketName)) {qCritical()<<"Cannot register Sung instance:"<<server.errorString();return 1;}
  }
  QQuickStyle::setStyle("Basic");
  QFont font(QFontDatabase::families().contains("Google Sans Flex")
                 ? "Google Sans Flex"
                 : "Noto Sans");
  font.setPixelSize(14);
  app.setFont(font);
  qmlRegisterType<RowSelection>("Sung.Native", 1, 0, "RowSelection");
  qmlRegisterType<RoundedArt>("Sung.Native", 1, 0, "RoundedArt");
  qmlRegisterType<MotionBackdrop>("Sung.Native", 1, 0, "MotionBackdrop");
  MotionArtwork motionArtwork;
  qmlRegisterUncreatableType<MotionArtwork>("Sung.Native",1,0,"MotionArtwork","Shared current artwork");
  // The backend is built after the engine has started on the interface, and
  // still has to outlive it: the interface binds to it until it is torn down.
  std::optional<Backend> backendStorage;
  DesktopTheme desktopTheme;
  WindowResources windowResources;
  std::optional<DesktopIntegration> desktopStorage;
  QQmlApplicationEngine engine;
  // The interface's types load on the QML engine's own thread while the
  // backend opens the audio outputs and reads the library, rather than after
  // it; load() below picks up whatever is still in flight instead of starting
  // over. From main() to the interface loaded, on the desktop: 231.8 to
  // 213.7 ms with an empty library; offscreen with 10,000 songs, 392.7 to
  // 370.0 ms.
  // https://doc.qt.io/qt-6/qqmlcomponent.html#CompilationMode-enum
  QQmlComponent interfaceTypes(&engine, QUrl("qrc:/qml/Main.qml"), QQmlComponent::Asynchronous);
  Backend &backend = backendStorage.emplace();
  // Tests, smoke runs and screenshots start from a clean profile; none of
  // them should have to dismiss the first-run flow.
  if (args.contains("--isolated")) backend.setOnboarded(true);
  QObject::connect(&backend,&Backend::artworkCacheCleared,&app,[]{RoundedArt::clearCaches();});
  const bool desktopEnabled = !args.contains("--isolated");
  DesktopIntegration &desktopIntegration=desktopStorage.emplace(&backend,desktopEnabled);
  if (desktopEnabled) {
    registerMpris(&backend,instance);
    // Closing to the tray hides the window; explicit Quit remains authoritative.
    // https://doc.qt.io/qt-6/qguiapplication.html#quitOnLastWindowClosed-prop
    app.setQuitOnLastWindowClosed(false);
  }
  engine.rootContext()->setContextProperty("windowResources", &windowResources);
  engine.rootContext()->setContextProperty("desktopIntegration", &desktopIntegration);
  engine.addImageProvider("symbols", new Symbols);
  engine.rootContext()->setContextProperty("app", &backend);
  engine.rootContext()->setContextProperty("motionArtwork", &motionArtwork);
  engine.rootContext()->setContextProperty("desktopTheme", &desktopTheme);
  QObject::connect(
      &engine, &QQmlApplicationEngine::objectCreationFailed, &app,
      [] { QCoreApplication::exit(1); }, Qt::QueuedConnection);
  engine.load(QUrl("qrc:/qml/Main.qml"));
  if (engine.rootObjects().isEmpty())
    return 1;
  auto window = qobject_cast<QQuickWindow *>(engine.rootObjects().first());
  // Reading the library and building the interface free a good deal of what
  // they allocate on the way; with 10,000 songs, 16 MiB on the desktop and 24
  // offscreen were still held for nothing once the window was up. The first
  // frame marks the end of that work.
  QMetaObject::Connection startupSettled;
  startupSettled = QObject::connect(window, &QQuickWindow::frameSwapped, &app, [&startupSettled] {
    QObject::disconnect(startupSettled);
    returnFreedMemory();
  }, Qt::QueuedConnection);
  // A session goes on freeing memory between the trims that follow startup
  // and saves. At the end of a tour of the interface 30 MiB was free but
  // still held, half of it in the arenas of worker threads, where the main
  // thread cannot reuse it. Once the application stops being the one in
  // use, that memory matters more to everything else than to Sung, and the
  // trim took under half a millisecond on that heap. Qt reports the change
  // from the window's focus, on Wayland as everywhere else.
  QObject::connect(&app, &QGuiApplication::applicationStateChanged, &app, [](Qt::ApplicationState state) {
    if (state != Qt::ApplicationActive)
      returnFreedMemory();
  });
  QObject::connect(&backend, &Backend::raiseRequested, window, [window] {
    QMetaObject::invokeMethod(window,"restorePlayer");
    window->raise();
    window->requestActivate();
  });
  if(args.contains("--mini"))QTimer::singleShot(0,window,[window]{QMetaObject::invokeMethod(window,"openMiniPlayer");});
  QObject::connect(&server, &QLocalServer::newConnection, &app, [&] {
    auto socket = server.nextPendingConnection();
    QObject::connect(socket, &QLocalSocket::readyRead, &backend, [&, socket] {
      auto v = QString::fromUtf8(socket->readAll());
      if (v.startsWith("https://"))
        backend.openLink(v);
      if(v=="--mini")QMetaObject::invokeMethod(window,"openMiniPlayer");
      else emit backend.raiseRequested();
      socket->disconnectFromServer();
    });
    QObject::connect(socket, &QLocalSocket::disconnected, socket,
                     &QObject::deleteLater);
  });
  #ifdef SUNG_DIAGNOSTICS
  if(args.contains("--desktop-ui-test")){backend.setOnboarded(true);QTimer::singleShot(0,&app,[&]{runDesktopUiTests(&backend,window,&desktopIntegration);});return app.exec();}
  if(args.contains("--qqmusic-ui-test")){QTimer::singleShot(0,&app,[&]{runQQMusicUiTests(&backend,window);});return app.exec();}
  #endif
  if (args.contains("--smoke-local")) {
    const int i = args.indexOf("--smoke-local");
    if (i + 1 < args.size())
      backend.localTestSource(QUrl::fromLocalFile(args[i + 1]));
    QTimer::singleShot(4000, &app, [&] {
      bool ok = backend.playing() && backend.position() > 500;
      qInfo() << "LOCAL_PLAYBACK" << ok << backend.position();
      app.exit(ok ? 0 : 2);
    });
  } else if (!args.contains("--offline")) {
    bool linked = false;
    for (const auto &a : args)
      if (a.startsWith("https://")) {
        backend.openLink(a);
        linked = true;
        break;
      }
    if (!linked)
      QTimer::singleShot(0, &backend, &Backend::openStartPage);
  }
  if (args.contains("--screenshot")) {
    const int i = args.indexOf("--screenshot");
    const auto path = args.value(i + 1);
    QTimer::singleShot(2500, &app,
                       [window, path] { window->grabWindow().save(path); });
    QTimer::singleShot(3000, &app, &QCoreApplication::quit);
  }
  return app.exec();
}
