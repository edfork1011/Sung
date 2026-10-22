#include "desktopintegration.h"
#include "backend.h"
#include <QAction>
#include <QCoreApplication>
#include <QDBusConnection>
#include <QDBusServiceWatcher>
#include <QIcon>
#include <QMenu>
#include <QSystemTrayIcon>
#include <QTimer>

DesktopIntegration::DesktopIntegration(Backend *backend, bool enabled, QObject *parent)
    : QObject(parent), m_backend(backend) {
  if (!enabled) return;
  // QSystemTrayIcon implements the StatusNotifierItem protocol used by
  // Caelestia. Its platform menu follows the desktop's own tray conventions.
  // https://doc.qt.io/qt-6/qsystemtrayicon.html
  m_menu = std::make_unique<QMenu>();
  const auto add = [this](const QString &label, const char *name, auto action) {
    auto item = m_menu->addAction(label);
    item->setObjectName(name);
    connect(item, &QAction::triggered, this, action);
    return item;
  };
  add("Show Sung", "trayShowAction", [this] { emit m_backend->raiseRequested(); });
  m_menu->addSeparator();
  m_toggle = add("Play", "trayToggleAction", [this] { m_backend->toggle(); });
  m_previous = add("Previous", "trayPreviousAction", [this] { m_backend->previous(); });
  m_next = add("Next", "trayNextAction", [this] { m_backend->next(); });
  m_menu->addSeparator();
  add("Quit Sung", "trayQuitAction", [] { QCoreApplication::quit(); });
  m_tray = std::make_unique<QSystemTrayIcon>(QIcon(":/assets/sung.png"));
  m_tray->setContextMenu(m_menu.get());
  connect(m_tray.get(), &QSystemTrayIcon::activated, this, [this](auto reason) {
    if (reason == QSystemTrayIcon::Trigger || reason == QSystemTrayIcon::DoubleClick)
      emit m_backend->raiseRequested();
    else if (reason == QSystemTrayIcon::MiddleClick)
      m_backend->toggle();
  });
  connect(backend, &Backend::trackChanged, this, &DesktopIntegration::refresh);
  connect(backend, &Backend::playbackChanged, this, &DesktopIntegration::refresh);
  connect(backend, &Backend::settingsChanged, this, &DesktopIntegration::refresh);
  connect(backend->queue(), &Entries::countChanged, this, &DesktopIntegration::refresh);
  // Qt automatically adds a visible icon when a tray host starts later.
  m_tray->show();
  auto watcher = new QDBusServiceWatcher("org.kde.StatusNotifierWatcher", QDBusConnection::sessionBus(),
      QDBusServiceWatcher::WatchForOwnerChange, this);
  connect(watcher, &QDBusServiceWatcher::serviceOwnerChanged, this, [this] {
    QTimer::singleShot(0, this, &DesktopIntegration::refreshAvailability);
  });
  refreshAvailability();
  refresh();
}
DesktopIntegration::~DesktopIntegration() = default;
bool DesktopIntegration::available() const {
  return m_tray && QSystemTrayIcon::isSystemTrayAvailable();
}
bool DesktopIntegration::keepInBackground() const {
  // Never hide the last window if the desktop offers no way to recover it.
  return m_backend->closeToTray() && available();
}
void DesktopIntegration::refreshAvailability() {
  const bool next = available();
  if (next == m_available) return;
  const bool lost = m_available && !next;
  m_available = next;
  emit availabilityChanged();
  if (lost) emit m_backend->raiseRequested();
}
void DesktopIntegration::refresh() {
  if (!m_tray) return;
  const auto song = m_backend->current();
  QString label = "Sung";
  if (!song.isEmpty()) label += " · " + song.value("title").toString() + " — " + song.value("artist").toString();
  m_tray->setToolTip(label.left(250).toHtmlEscaped());
  m_toggle->setText(m_backend->playing() || m_backend->resolving() ? "Pause" : "Play");
  m_toggle->setEnabled(m_backend->queue()->count() > 0);
  m_previous->setEnabled(m_backend->currentIndex() >= 0);
  m_next->setEnabled(m_backend->currentIndex() >= 0 &&
      (m_backend->currentIndex() + 1 < m_backend->queue()->count() || m_backend->repeat() == 1 ||
       m_backend->shuffle() || (m_backend->autoplay() && song.value("source") == "qqmusic")));
}
