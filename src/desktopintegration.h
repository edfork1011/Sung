#pragma once
#include <QObject>
#include <memory>

class Backend;
class QAction;
class QMenu;
class QSystemTrayIcon;

class DesktopIntegration : public QObject {
  Q_OBJECT
  Q_PROPERTY(bool available READ available NOTIFY availabilityChanged)
public:
  explicit DesktopIntegration(Backend *backend, bool enabled, QObject *parent = nullptr);
  ~DesktopIntegration() override;
  bool available() const;
  Q_INVOKABLE bool keepInBackground() const;
signals:
  void availabilityChanged();
private:
  void refresh();
  void refreshAvailability();
  Backend *m_backend;
  bool m_available = false;
  std::unique_ptr<QMenu> m_menu;
  std::unique_ptr<QSystemTrayIcon> m_tray;
  QAction *m_toggle = nullptr, *m_previous = nullptr, *m_next = nullptr;
};
