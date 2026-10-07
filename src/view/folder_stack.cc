#include "folder_stack.h"

#include <QDir>
#include <QEasingCurve>
#include <QFileIconProvider>
#include <QFileDialog>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QVBoxLayout>
#include <QLabel>
#include <QMouseEvent>
#include <QPainter>
#include <QPalette>
#include <QFont>
#include <QProcess>
#include <QPixmap>
#include <QPropertyAnimation>
#include <QGraphicsOpacityEffect>
#include <QTimer>
#include <QPushButton>
#include <QStandardPaths>
#include <QSettings>
#include <QWidget>
#include <QWidgetAction>

#include "dock_panel.h"

namespace crystaldock {

FolderStack::FolderStack(DockPanel* parent,
                         MultiDockModel* model,
                         Qt::Orientation orientation,
                         int minSize,
                         int maxSize,
                         const QString& stackId,
                         const QString& initialLabel,
                         QStandardPaths::StandardLocation initialLocation)
    : IconBasedDockItem(parent,
                        model,
                        initialLabel,
                        orientation,
                        initialLocation == QStandardPaths::DownloadLocation
                            ? "folder-download"
                            : "folder",
                        minSize,
                        maxSize),
      stackId_(stackId),
      folderPath_(
          QStandardPaths::writableLocation(initialLocation)),
      folderLabel_(initialLabel) {

  if (folderPath_.isEmpty()) {
    folderPath_ = QDir::homePath() + "/Downloads";
  }

  // Load Downloads preferences.
  {
    const QString configPath =
        QDir::homePath() +
        "/.config/macaque-dock/KDE/appearance.conf";

    QSettings settings(configPath, QSettings::IniFormat);

    sortByNewest_ =
        settings.value(
            stackId_ + "/sortByNewest",
            true).toBool();

    maximumItems_ =
        settings.value(
            stackId_ + "/maximumItems",
            8).toInt();

    if (maximumItems_ != 5 &&
        maximumItems_ != 8 &&
        maximumItems_ != 12) {
      maximumItems_ = 8;
    }

    const QString selectedFolder =
        settings.value(
            stackId_ + "/folder",
            initialLabel).toString();

    if (selectedFolder == "Custom") {
      const QString customPath =
          settings.value(
              stackId_ + "/customFolderPath",
              "").toString();

      if (!customPath.isEmpty() &&
          QDir(customPath).exists()) {
        folderPath_ = customPath;

        QFileInfo customInfo(customPath);
        folderLabel_ = customInfo.fileName();

        if (folderLabel_.isEmpty()) {
          folderLabel_ = customPath;
        }
      } else {
        folderPath_ =
            QStandardPaths::writableLocation(
                QStandardPaths::DownloadLocation);
        folderLabel_ = "Downloads";
      }

    } else if (selectedFolder == "Documents") {
      folderPath_ =
          QStandardPaths::writableLocation(
              QStandardPaths::DocumentsLocation);
      folderLabel_ = "Documents";

    } else if (selectedFolder == "Pictures") {
      folderPath_ =
          QStandardPaths::writableLocation(
              QStandardPaths::PicturesLocation);
      folderLabel_ = "Pictures";

    } else if (selectedFolder == "Desktop") {
      folderPath_ =
          QStandardPaths::writableLocation(
              QStandardPaths::DesktopLocation);
      folderLabel_ = "Desktop";

    } else {
      folderPath_ =
          QStandardPaths::writableLocation(
              QStandardPaths::DownloadLocation);
      folderLabel_ = "Downloads";
    }
  }

  // Custom transparent Downloads fan popup.
  fanPopup_ = new QWidget(
      nullptr,
      Qt::Popup |
      Qt::FramelessWindowHint |
      Qt::NoDropShadowWindowHint);

  fanPopup_->setAttribute(
      Qt::WA_TranslucentBackground,
      true);

  fanPopup_->setAttribute(
      Qt::WA_NoSystemBackground,
      true);

  fanPopup_->setAutoFillBackground(false);

  fanPopup_->setStyleSheet(
      "QWidget#MacaqueDownloadsFan {"
      " background: transparent;"
      " border: none;"
      "}");

  fanPopup_->setObjectName(
      "MacaqueDownloadsFan");

  connect(&contextMenu_,
          &QMenu::aboutToHide,
          this,
          [this]() {
            parent_->setShowingPopup(false);
          });
}

void FolderStack::draw(QPainter* painter) const {
  // Draw the normal Downloads icon first.
  IconBasedDockItem::draw(painter);

  QDir dir(folderPath_);

  const QFileInfoList entries =
      dir.entryInfoList(
          QDir::Dirs |
          QDir::Files |
          QDir::NoDotAndDotDot);

  const int count = entries.size();

  if (count <= 0) {
    return;
  }

  // Display 99+ rather than making the badge excessively wide.
  const QString countText =
      count > 99
          ? QStringLiteral("99+")
          : QString::number(count);

  painter->save();

  // Badge size follows the current zoomed icon size.
  const int badgeHeight =
      qMax(18, size_ / 3);

  const int badgeWidth =
      count > 99
          ? badgeHeight + 10
          : badgeHeight;

  // Upper-right corner of the Downloads icon.
  const QRect badgeRect(
      left_ + size_ - badgeWidth,
      top_,
      badgeWidth,
      badgeHeight);

  // Shadow.
  painter->setPen(Qt::NoPen);
  painter->setBrush(QColor(0, 0, 0, 110));

  painter->drawRoundedRect(
      badgeRect.translated(1, 2),
      badgeHeight / 2.0,
      badgeHeight / 2.0);

  // Macaque badge.
  painter->setBrush(QColor(225, 55, 55, 245));

  painter->drawRoundedRect(
      badgeRect,
      badgeHeight / 2.0,
      badgeHeight / 2.0);

  // Count.
  QFont font = painter->font();

  font.setBold(true);
  font.setPixelSize(
      qMax(10, badgeHeight - 7));

  painter->setFont(font);
  painter->setPen(Qt::white);

  painter->drawText(
      badgeRect,
      Qt::AlignCenter,
      countText);

  painter->restore();
}

void FolderStack::mousePressEvent(QMouseEvent* e) {
  if (e->button() == Qt::LeftButton) {
    rebuildMenu();

    parent_->setShowingPopup(true);
    showFanPopup();

  } else if (e->button() == Qt::RightButton) {
    rebuildContextMenu();

    parent_->setShowingPopup(true);
    showPopupMenu(&contextMenu_);
  }
}

void FolderStack::rebuildMenu() {
  // Remove the previous fan contents.
  if (fanPopup_->layout()) {
    delete fanPopup_->layout();
  }

  const auto oldChildren =
      fanPopup_->findChildren<QWidget*>(
          QString(),
          Qt::FindDirectChildrenOnly);

  for (QWidget* child : oldChildren) {
    delete child;
  }

  auto* mainLayout =
      new QVBoxLayout(fanPopup_);

  mainLayout->setContentsMargins(4, 4, 4, 4);
  mainLayout->setSpacing(2);

  QDir dir(folderPath_);

  QDir::SortFlags sortFlags;

  if (sortByNewest_) {
    sortFlags = QDir::Time;
  } else {
    sortFlags =
        QDir::DirsFirst |
        QDir::Name |
        QDir::IgnoreCase;
  }

  const QFileInfoList entries =
      dir.entryInfoList(
          QDir::Dirs |
          QDir::Files |
          QDir::NoDotAndDotDot,
          sortFlags);

  QFileIconProvider iconProvider;

  const int visibleItems =
      qMin(
          maximumItems_,
          static_cast<int>(entries.size()));

  for (int i = 0; i < visibleItems; ++i) {
    const QFileInfo info = entries.at(i);

    auto* container =
        new QWidget(fanPopup_);

    container->setAttribute(
        Qt::WA_TranslucentBackground,
        true);

    container->setAutoFillBackground(false);

    // Preserve our curved fan.
    const int level =
        visibleItems - i - 1;

    const int indent =
        (level * level * 3) +
        (level * 5);

    auto* outerLayout =
        new QHBoxLayout(container);

    outerLayout->setContentsMargins(
        indent,
        1,
        6,
        1);

    outerLayout->setSpacing(0);

    auto* button =
        new QPushButton(container);

    QIcon displayIcon =
        iconProvider.icon(info);

    // Image files get real thumbnails.
    if (info.isFile()) {
      const QString suffix =
          info.suffix().toLower();

      const bool isImage =
          suffix == "png"  ||
          suffix == "jpg"  ||
          suffix == "jpeg" ||
          suffix == "webp" ||
          suffix == "bmp"  ||
          suffix == "gif";

      if (isImage) {
        QPixmap preview(
            info.absoluteFilePath());

        if (!preview.isNull()) {
          preview =
              preview.scaled(
                  48,
                  48,
                  Qt::KeepAspectRatio,
                  Qt::SmoothTransformation);

          displayIcon = QIcon(preview);
        }
      }
    }

    button->setIcon(displayIcon);
    button->setIconSize(QSize(32, 32));
    button->setText(info.fileName());

    button->setMinimumHeight(0);
    button->setMinimumWidth(0);

    button->setCursor(
        Qt::PointingHandCursor);

    // Compact item style matching the bottom action rows.
    button->setStyleSheet(R"(
      QPushButton {
        background: transparent;
        color: white;
        border: none;
        padding: 7px;
        text-align: left;
      }

      QPushButton:hover {
        background:
          rgba(255, 255, 255, 30);
        border-radius: 8px;
      }

      QPushButton:pressed {
        background:
          rgba(255, 255, 255, 45);
        border-radius: 8px;
      }
    )");

    const QString path =
        info.absoluteFilePath();

    connect(
        button,
        &QPushButton::clicked,
        this,
        [this, path]() {
          hideFanPopup();
          openPath(path);
        });

    outerLayout->addWidget(button);

    mainLayout->addWidget(container);

    // Preserve the staggered fade animation.
    auto* opacity =
        new QGraphicsOpacityEffect(button);

    button->setGraphicsEffect(opacity);
    opacity->setOpacity(0.0);

    // Open from the Downloads icon upward.
    // Lower rows appear first, followed quickly by
    // each row above them.
    const int delay =
        (visibleItems - i - 1) * 45;

    QTimer::singleShot(
        delay,
        button,
        [button, opacity]() {
          auto* fade =
              new QPropertyAnimation(
                  opacity,
                  "opacity",
                  button);

          fade->setDuration(145);
          fade->setStartValue(0.0);
          fade->setEndValue(1.0);
          fade->setEasingCurve(
              QEasingCurve::OutCubic);

          fade->start(
              QAbstractAnimation::
                  DeleteWhenStopped);
        });
  }

  if (entries.isEmpty()) {
    auto* label =
        new QLabel(
            "Downloads is empty",
            fanPopup_);

    label->setMinimumWidth(260);

    label->setStyleSheet(R"(
      QLabel {
        background-color:
          rgba(32, 32, 36, 225);

        color:
          rgba(255, 255, 255, 150);

        border:
          1px solid
          rgba(255, 255, 255, 50);

        border-radius: 12px;
        padding: 14px;
      }
    )");

    mainLayout->addWidget(label);
  }

  // Bottom buttons replace the old QMenu actions.
  if (entries.size() > maximumItems_) {
    auto* showAll =
        new QPushButton(
            QIcon::fromTheme(
                "view-more-symbolic"),
            "Show All Downloads",
            fanPopup_);

    showAll->setStyleSheet(R"(
      QPushButton {
        background: transparent;
        color: white;
        border: none;
        padding: 7px;
        text-align: left;
      }

      QPushButton:hover {
        background:
          rgba(255, 255, 255, 30);
        border-radius: 8px;
      }
    )");

    connect(
        showAll,
        &QPushButton::clicked,
        this,
        [this]() {
          hideFanPopup();
          openDownloads();
        });

    mainLayout->addWidget(showAll);
  }

  auto* openFolder =
      new QPushButton(
          QIcon::fromTheme("folder-open"),
          "Open Downloads",
          fanPopup_);

  openFolder->setStyleSheet(R"(
    QPushButton {
      background: transparent;
      color: white;
      border: none;
      padding: 7px;
      text-align: left;
    }

    QPushButton:hover {
      background:
        rgba(255, 255, 255, 30);
      border-radius: 8px;
    }
  )");

  connect(
      openFolder,
      &QPushButton::clicked,
      this,
      [this]() {
        hideFanPopup();
        openDownloads();
      });

  mainLayout->addWidget(openFolder);

  fanPopup_->adjustSize();
}


void FolderStack::showFanPopup() {
  if (!fanPopup_) {
    return;
  }

  fanPopup_->adjustSize();

  /*
   * Position the popup so its bottom is immediately
   * above the Downloads icon.
   */
  const QPoint dockPoint =
      parent_->mapToGlobal(
          QPoint(left_, top_));

  // Anchor the bottom of the fan to the center
  // of the Downloads icon.
  const int iconCenterX =
      dockPoint.x() + (size_ / 2);

  // The lower rows sit toward the left side of the popup,
  // so don't center the entire wide fan over the icon.
  const int anchorOffset = 32;

  const int x =
      iconCenterX - anchorOffset;

  // Leave a tiny gap above the dock icon.
  const int y =
      dockPoint.y() -
      fanPopup_->height() -
      4;

  fanPopup_->move(x, y);

  parent_->setShowingPopup(true);

  fanPopup_->show();
  fanPopup_->raise();
}


void FolderStack::hideFanPopup() {
  if (fanPopup_) {
    fanPopup_->hide();
  }

  parent_->setShowingPopup(false);
}


void FolderStack::rebuildContextMenu() {
  contextMenu_.clear();

  QAction* open =
      contextMenu_.addAction(
          QIcon::fromTheme("folder-open"),
          "Open Downloads");

  connect(open,
          &QAction::triggered,
          this,
          &FolderStack::openDownloads);

  contextMenu_.addSeparator();

  QMenu* folderMenu =
      contextMenu_.addMenu("Folder");

  struct FolderChoice {
    const char* name;
    QStandardPaths::StandardLocation location;
  };

  const FolderChoice choices[] = {
      {"Downloads",
       QStandardPaths::DownloadLocation},

      {"Documents",
       QStandardPaths::DocumentsLocation},

      {"Pictures",
       QStandardPaths::PicturesLocation},

      {"Desktop",
       QStandardPaths::DesktopLocation}
  };

  for (const FolderChoice& choice : choices) {
    QAction* folderAction =
        folderMenu->addAction(choice.name);

    folderAction->setCheckable(true);

    folderAction->setChecked(
        folderLabel_ == choice.name);

    connect(
        folderAction,
        &QAction::triggered,
        this,
        [this, choice]() {
          setFolder(
              choice.location,
              choice.name);
        });
  }

  folderMenu->addSeparator();

  QAction* chooseCustom =
      folderMenu->addAction(
          QIcon::fromTheme("folder-open"),
          "Choose Custom Folder...");

  connect(
      chooseCustom,
      &QAction::triggered,
      this,
      [this]() {
        const QString selected =
            QFileDialog::getExistingDirectory(
                parent_,
                "Choose Folder",
                folderPath_,
                QFileDialog::ShowDirsOnly |
                QFileDialog::DontResolveSymlinks);

        if (selected.isEmpty()) {
          return;
        }

        folderPath_ = selected;

        QFileInfo info(selected);
        folderLabel_ = info.fileName();

        if (folderLabel_.isEmpty()) {
          folderLabel_ = selected;
        }

        QSettings settings(
            QDir::homePath() +
                "/.config/macaque-dock/KDE/appearance.conf",
            QSettings::IniFormat);

        settings.setValue(
            stackId_ + "/folder",
            "Custom");

        settings.setValue(
            stackId_ + "/customFolderPath",
            folderPath_);

        settings.sync();

        parent_->update();
      });

  contextMenu_.addSeparator();

  QAction* newest =
      contextMenu_.addAction("Sort by Newest");

  newest->setCheckable(true);
  newest->setChecked(sortByNewest_);

  connect(newest,
          &QAction::triggered,
          this,
          [this]() {
            sortByNewest_ = true;

          QSettings settings(
              QDir::homePath() +
                  "/.config/macaque-dock/KDE/appearance.conf",
              QSettings::IniFormat);

          settings.setValue(
              stackId_ + "/sortByNewest",
              true);
          settings.sync();
          });

  QAction* name =
      contextMenu_.addAction("Sort by Name");

  name->setCheckable(true);
  name->setChecked(!sortByNewest_);

  connect(name,
          &QAction::triggered,
          this,
          [this]() {
            sortByNewest_ = false;

          QSettings settings(
              QDir::homePath() +
                  "/.config/macaque-dock/KDE/appearance.conf",
              QSettings::IniFormat);

          settings.setValue(
              stackId_ + "/sortByNewest",
              false);
          settings.sync();
          });

  contextMenu_.addSeparator();

  QMenu* amountMenu =
      contextMenu_.addMenu("Number of Items");

  for (const int amount : {5, 8, 12}) {
    QAction* action =
        amountMenu->addAction(
            QString::number(amount));

    action->setCheckable(true);
    action->setChecked(
        maximumItems_ == amount);

    connect(action,
            &QAction::triggered,
            this,
            [this, amount]() {
              maximumItems_ = amount;

            QSettings settings(
                QDir::homePath() +
                    "/.config/macaque-dock/KDE/appearance.conf",
                QSettings::IniFormat);

            settings.setValue(
                stackId_ + "/maximumItems",
                amount);
            settings.sync();
            });
  }

  // Downloads is the permanent default stack.
  if (stackId_ != "Downloads") {
    contextMenu_.addSeparator();

    QAction* remove =
        contextMenu_.addAction(
            QIcon::fromTheme("list-remove"),
            "Remove Folder from Dock");

    connect(
        remove,
        &QAction::triggered,
        this,
        [this]() {
          const QString id = stackId_;
          hideFanPopup();

          QTimer::singleShot(
              0,
              parent_,
              [parent = parent_, id]() {
                parent->removeFolderStack(id);
              });
        });
  }
}

void FolderStack::setFolder(
    QStandardPaths::StandardLocation location,
    const QString& label) {

  QString path =
      QStandardPaths::writableLocation(location);

  if (path.isEmpty()) {
    return;
  }

  folderPath_ = path;
  folderLabel_ = label;

  QSettings settings(
      QDir::homePath() +
          "/.config/macaque-dock/KDE/appearance.conf",
      QSettings::IniFormat);

  settings.setValue(
      stackId_ + "/folder",
      folderLabel_);

  settings.sync();

  // Update the dock immediately so the badge reflects
  // the newly selected folder.
  parent_->update();
}


void FolderStack::openPath(const QString& path) {
  QProcess::startDetached(
      "xdg-open",
      QStringList() << path);
}

void FolderStack::openDownloads() {
  QProcess::startDetached(
      "xdg-open",
      QStringList() << folderPath_);
}

}  // namespace crystaldock
