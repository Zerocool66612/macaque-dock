#ifndef MACAQUEDOCK_FOLDER_STACK_H_
#define MACAQUEDOCK_FOLDER_STACK_H_

#include <QMenu>
#include <QObject>
#include <QString>
#include <QStandardPaths>
#include <QWidget>

#include "icon_based_dock_item.h"

namespace crystaldock {

class FolderStack : public QObject, public IconBasedDockItem {
  Q_OBJECT

 public:
  FolderStack(DockPanel* parent,
              MultiDockModel* model,
              Qt::Orientation orientation,
              int minSize,
              int maxSize);

  ~FolderStack() override = default;

  void draw(QPainter* painter) const override;
  void mousePressEvent(QMouseEvent* e) override;

  QString getLabel() const override {
    return folderLabel_;
  }

  QString getAppId() const override {
    return "macaque-downloads-stack";
  }

  bool beforeTask(const QString& program) override {
    return false;
  }

 private:
  void rebuildMenu();
  void showFanPopup();
  void hideFanPopup();
  void rebuildContextMenu();
  void openPath(const QString& path);
  void openDownloads();

  void setFolder(
      QStandardPaths::StandardLocation location,
      const QString& label);

  QString folderPath_;
  QString folderLabel_ = "Downloads";
  QWidget* fanPopup_ = nullptr;
  QMenu contextMenu_;

  bool sortByNewest_ = true;
  int maximumItems_ = 8;
};

}  // namespace crystaldock

#endif
