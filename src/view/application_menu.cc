/*
 * This file is part of Crystal Dock.
 * Copyright (C) 2022 Viet Dang (dangvd@gmail.com)
 *
 * Crystal Dock is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * Crystal Dock is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with Crystal Dock.  If not, see <http://www.gnu.org/licenses/>.
 */

#include "application_menu.h"

#include <algorithm>

#include <QApplication>
#include <QGuiApplication>
#include <QScreen>
#include <QShortcut>
#include <QKeySequence>
#include <QScrollArea>
#include <QGridLayout>
#include <QVBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QDialog>
#include <QDrag>
#include <QFont>
#include <QMimeData>
#include <QSettings>
#include <QStringBuilder>
#include <QTimer>
#include <QUrl>
#include <KWindowEffects>

#include "display/window_system.h"

#include "dock_panel.h"
#include "program.h"
#include <utils/draw_utils.h>
#include <utils/menu_utils.h>

namespace crystaldock {

int ApplicationMenuStyle::pixelMetric(
    PixelMetric metric, const QStyleOption *option, const QWidget *widget)
    const {
  if (metric == QStyle::PM_SmallIconSize) {
    return model_->applicationMenuIconSize();
  }
  return QProxyStyle::pixelMetric(metric, option, widget);
}

ApplicationMenu::ApplicationMenu(
    DockPanel *parent, MultiDockModel* model, Qt::Orientation orientation,
    int minSize, int maxSize)
    : IconBasedDockItem(parent, model, "" /* label */, orientation, model->applicationMenuIcon(),
                        minSize, maxSize),
      showingMenu_(false),
      style_(model) {
  menu_.setAttribute(Qt::WA_TranslucentBackground);
  menu_.setStyle(&style_);
  menu_.setStyleSheet(getStyleSheet());

  loadConfig();
  buildMenu();

  createContextMenu();

  connect(&menu_, &QMenu::aboutToHide, this,
          [this]() {
            showingMenu_ = false;
            parent_->setShowingPopup(false);
            parent_->update();
          });
  connect(&contextMenu_, &QMenu::aboutToHide, this,
          [this]() {
            parent_->setShowingPopup(false);
          });
  connect(model_, SIGNAL(applicationMenuConfigChanged()),
          this, SLOT(reloadMenu()));
}

void ApplicationMenu::draw(QPainter* painter) const {
  if (showingMenu_) {
    const auto x = left_ + getWidth() / 2;
    const auto y = top_ + getHeight() / 2;
    if (parent_->isGlass()) {
      const QColor baseColor = model_->activeIndicatorColor();
      // Size (width if horizontal, or height if vertical) of the indicator.
      const auto size = DockPanel::kIndicatorSizeGlass;
      drawIndicator(orientation_, x, parent_->taskIndicatorPos(),
                    parent_->taskIndicatorPos(), y,
                    size, DockPanel::k3DPanelThickness, baseColor, painter);
    } else if (parent_->isFlat2D()) {
      const auto baseColor = model_->activeIndicatorColor2D();
      const auto size = DockPanel::kIndicatorSizeFlat2D;
      drawIndicatorFlat2D(orientation_, x, parent_->taskIndicatorPos(),
                          parent_->taskIndicatorPos(), y,
                          size, baseColor, painter);
    } else {  // Metal 2D.
      const auto baseColor = model_->activeIndicatorColorMetal2D();
      const auto size = DockPanel::kIndicatorSizeMetal2D;
      drawIndicatorMetal2D(parent_->position(), x, parent_->taskIndicatorPos(),
                           parent_->taskIndicatorPos(), y,
                           size, baseColor, painter);
    }
  }
  IconBasedDockItem::draw(painter);
}

void ApplicationMenu::mousePressEvent(QMouseEvent *e) {
  if (e->button() == Qt::LeftButton) {
    parent_->setShowingPopup(true);
    // Acknowledge.
    showingMenu_ = true;
    parent_->update();

    toggleLaunchpad();
  } else if (e->button() == Qt::RightButton) {
    showPopupMenu(&contextMenu_);
  }
}

void ApplicationMenu::toggleLaunchpad() {

  if (launchpad_ && launchpad_->isVisible()) {
    launchpad_->hide();
    showingMenu_ = false;
    parent_->setShowingPopup(false);
    parent_->update();
    return;
  }

  parent_->setShowingPopup(true);
  showingMenu_ = true;
  parent_->update();
  showLaunchpad();
}

void ApplicationMenu::reloadMenu() {
  menu_.clear();
  searchMenu_ = nullptr;
  buildMenu();
}

void ApplicationMenu::searchApps(const QString& searchText_) {
  if (searchMenu_ == nullptr) {
    return;
  }

  QString text = searchText_.trimmed();
  if (text.isEmpty()) {
    resetSearchMenu();
    return;
  }

  const auto actions = searchMenu_->actions();
  for (int i = 1; i < actions.size(); ++i) {
    searchMenu_->removeAction(actions[i]);
  }

  // We need to limit max number of results to avoid the sub-menu being pushed up too much.
  for (const auto& entry : model_->searchApplications(text, maxNumResults_)) {
    addEntry(entry, searchMenu_);
  }
  if (parent_->isBottom()) {
    // Work-around for sub-menu alignment issue on Wayland.
    patchMenu(maxNumResults_ + 1, model_->applicationMenuIconSize(), searchMenu_);
  }
}

bool ApplicationMenu::eventFilter(QObject* object, QEvent* event) {
  if (event->type() == QEvent::MouseButtonPress) {
    auto* menu = qobject_cast<QMenu*>(object);
    QMouseEvent* mouseEvent = dynamic_cast<QMouseEvent*>(event);

    if (menu && mouseEvent) {
      auto* activeItem = menu->activeAction();
      if (mouseEvent->button() == Qt::LeftButton && activeItem) {
        startMousePos_ = mouseEvent->pos();
        draggedEntry_ = activeItem->data().toString();
      }
    }
  } else if (event->type() == QEvent::MouseMove) {
    QMouseEvent* mouseEvent = dynamic_cast<QMouseEvent*>(event);
    if (mouseEvent && mouseEvent->buttons() & Qt::LeftButton) {
      const int distance
          = (mouseEvent->pos() - startMousePos_).manhattanLength();
      if (distance >= QApplication::startDragDistance()
          && !draggedEntry_.isEmpty()) {
        // Start drag.
        QMimeData* mimeData = new QMimeData;
        mimeData->setData("text/uri-list",
                          QUrl::fromLocalFile(draggedEntry_).toEncoded());

        QDrag* drag = new QDrag(this);
        drag->setMimeData(mimeData);
        drag->exec(Qt::CopyAction);
      }
    }
  }

  return QObject::eventFilter(object, event);
}

QString ApplicationMenu::getStyleSheet() {
  QColor bgColor = model_->backgroundColor();
  bgColor.setAlphaF(model_->applicationMenuBackgroundAlpha());
  bgColor = bgColor.darker();
  QColor borderColor = model_->borderColor();
  return " \
QMenu { \
  background-color: " % bgColor.name(QColor::HexArgb) % ";"
" margin: 1px; \
  padding: 2px; \
  border: 1px transparent; \
  border-radius: 3px; \
} \
\
QMenu::item { \
  font: bold; \
  color: white; \
  background-color: transparent; \
  padding: 4px 45px 4px 45px; \
} \
\
QMenu::item:selected { \
  background-color: " % bgColor.name(QColor::HexArgb) % ";"
" border: 1px solid " % borderColor.name() % ";"
" border-radius: 3px; \
} \
\
QMenu::separator { \
  margin: 5px; \
  height: 1px; \
  background: " % borderColor.name() % ";"
"}";
}

void ApplicationMenu::loadConfig() {
  setLabel(model_->applicationMenuName());
  font_ = menu_.font();
  font_.setPointSize(model_->applicationMenuFontSize());
  font_.setBold(true);
  menu_.setFont(font_);
}

void ApplicationMenu::buildMenu() {
  addSearchMenu();
  menu_.addSeparator();
  addToMenu(model_->applicationMenuCategories());
  menu_.addSeparator();
  addToMenu(model_->applicationMenuSystemCategories());
  const auto numSubMenus = menu_.actions().size();
  for (int i = 0; i < numSubMenus; ++i) {
    QMenu* menu = menu_.actions()[i]->menu();
    if (menu != nullptr && parent_->isBottom()) {
      // Work-around for sub-menu alignment issue on Wayland.
      patchMenu(numSubMenus - i, model_->applicationMenuIconSize(), menu);
    }
  }
  maxNumResults_ = menu_.actions().size() - 2;
}

void ApplicationMenu::addSearchMenu() {
  searchMenu_ = menu_.addMenu(loadIcon("edit-find"), "Search");
  searchMenu_->setAttribute(Qt::WA_TranslucentBackground);
  searchMenu_->setStyle(&style_);
  searchMenu_->setFont(font_);
  searchMenu_->installEventFilter(this);

  searchText_ = new QLineEdit(searchMenu_);
  searchText_->setMinimumWidth(250);
  searchText_->setPlaceholderText("Type here to search");
  // A work-around as using QWidgetAction somehow causes a memory issue
  // when quitting the dock.
  searchMenu_->addAction(loadIcon("edit-find"), "                 ");
  connect(searchText_, SIGNAL(textEdited(const QString&)),
          this, SLOT(searchApps(const QString&)));
}

void ApplicationMenu::addToMenu(const std::vector<Category>& categories) {
  for (const auto& category : categories) {
    if (category.name == ApplicationMenuConfig::kUncategorized || category.entries.empty()) {
      continue;
    }

    QMenu* menu = menu_.addMenu(loadIcon(category.icon), category.displayName);
    menu->setAttribute(Qt::WA_TranslucentBackground);
    menu->setStyle(&style_);
    menu->setFont(font_);
    menu->installEventFilter(this);
    for (const auto& entry : category.entries) {
      addEntry(entry, menu);
    }
  }
}

void ApplicationMenu::addEntry(const ApplicationEntry &entry, QMenu *menu) {
  if (entry.hidden) {
    return;
  }

  QAction* action = menu->addAction(loadIcon(entry.icon), entry.name, this,
                  [entry]() {
                    Program::launch(entry.command);
                  });
  action->setData(entry.desktopFile);
}

void ApplicationMenu::resetSearchMenu() {
  searchText_->clear();
  searchText_->setFocus();
  const auto actions = searchMenu_->actions();
  for (int i = 1; i < actions.size(); ++i) {
    searchMenu_->removeAction(actions[i]);
  }
  if (parent_->isBottom()) {
    // Work-around for sub-menu alignment issue on Wayland.
    patchMenu(maxNumResults_ + 1, model_->applicationMenuIconSize(), searchMenu_);
  }
}

QIcon ApplicationMenu::loadIcon(const QString &icon) {
  return QIcon::fromTheme(icon);
}


void ApplicationMenu::showLaunchpad() {
  if (!launchpad_) {
    launchpad_ = new QDialog(parent_);
    launchpad_->setWindowTitle("Macaque Launchpad");
    launchpad_->setWindowFlags(
        Qt::Dialog |
        Qt::FramelessWindowHint);

    launchpad_->setAttribute(Qt::WA_TranslucentBackground);

    // Close Launchpad when the user clicks away from it.
    launchpad_->installEventFilter(this);

    // Esc closes Launchpad while keeping it as a normal centered dialog.
    auto* closeShortcut =
        new QShortcut(QKeySequence(Qt::Key_Escape), launchpad_);

    connect(closeShortcut, &QShortcut::activated,
            this, [this]() {
              launchpad_->hide();
              showingMenu_ = false;
              parent_->setShowingPopup(false);
              parent_->update();
            });

    auto* mainLayout = new QVBoxLayout(launchpad_);
    mainLayout->setContentsMargins(40, 32, 40, 32);
    mainLayout->setSpacing(18);

    auto* title = new QLabel("MacaqueOS Applications", launchpad_);
    title->setAlignment(Qt::AlignCenter);
    title->setStyleSheet(
        "font-size: 28px;"
        "font-weight: 600;"
        "color: white;"
        "background: transparent;");
    mainLayout->addWidget(title);

    launchpadSearch_ = new QLineEdit(launchpad_);
    launchpadSearch_->setPlaceholderText("Search applications");
    launchpadSearch_->setClearButtonEnabled(true);
    launchpadSearch_->setMinimumHeight(42);
    launchpadSearch_->setMaximumWidth(520);

    launchpadSearch_->setStyleSheet(
        "QLineEdit {"
        " color: white;"
        " background: rgba(255,255,255,28);"
        " border: 1px solid rgba(255,255,255,45);"
        " border-radius: 18px;"
        " padding: 7px 16px;"
        " font-size: 15px;"
        "}"
        "QLineEdit:focus {"
        " border: 1px solid rgba(255,255,255,100);"
        "}");

    auto* searchRow = new QWidget(launchpad_);
    auto* searchLayout = new QHBoxLayout(searchRow);
    searchLayout->setContentsMargins(0, 0, 0, 0);
    searchLayout->addStretch();
    searchLayout->addWidget(launchpadSearch_);
    searchLayout->addStretch();
    mainLayout->addWidget(searchRow);

    auto* scroll = new QScrollArea(launchpad_);
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);

    // MacaqueOS Launchpad scrolls vertically only.
    scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    scroll->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    scroll->setStyleSheet(
        "QScrollArea { background: transparent; border: none; }"
        "QScrollBar:vertical {"
        " background: transparent;"
        " width: 8px;"
        "}"
        "QScrollBar::handle:vertical {"
        " background: rgba(255,255,255,70);"
        " border-radius: 4px;"
        " min-height: 30px;"
        "}"
        "QScrollBar::add-line:vertical,"
        "QScrollBar::sub-line:vertical { height: 0px; }");

    launchpadGridWidget_ = new QWidget(scroll);
    launchpadGridWidget_->setStyleSheet("background: transparent;");

    launchpadGrid_ = new QGridLayout(launchpadGridWidget_);
    launchpadGrid_->setContentsMargins(20, 15, 20, 15);
    launchpadGrid_->setHorizontalSpacing(24);
    launchpadGrid_->setVerticalSpacing(24);
    launchpadGrid_->setAlignment(Qt::AlignTop | Qt::AlignHCenter);

    scroll->setWidget(launchpadGridWidget_);

    // Keep the application grid inside the visible Launchpad width.
    // Only vertical scrolling is allowed.
    launchpadGridWidget_->setSizePolicy(
        QSizePolicy::Ignored,
        QSizePolicy::Preferred);

    scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    scroll->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);

    mainLayout->addWidget(scroll, 1);

    // Match Launchpad to the Macaque Dock background color.
    // Use exactly the same background color and transparency
    // selected for Macaque Dock.
    const QColor launchpadColor = model_->backgroundColor();

    // Match the subtle Macaque Dock border.
    QColor launchpadBorder = model_->borderColor();
    launchpadBorder.setAlpha(90);

    launchpad_->setStyleSheet(
        QString(
            "QDialog {"
            " background-color: %1;"
            " border: 1px solid %2;"
            " border-radius: 26px;"
            "}")
            .arg(launchpadColor.name(QColor::HexArgb))
            .arg(launchpadBorder.name(QColor::HexArgb)));

    connect(launchpadSearch_, &QLineEdit::textChanged,
            this, [this](const QString& text) {
              rebuildLaunchpad(text);
            });
  }

  rebuildLaunchpad();

  QScreen* screen = parent_->screen();
  if (!screen) {
    screen = QGuiApplication::primaryScreen();
  }

  if (screen) {
    const QRect area = model_->fullScreenApplicationLauncher()
                           ? screen->geometry()
                           : screen->availableGeometry();

    if (model_->fullScreenApplicationLauncher()) {
      launchpad_->setGeometry(area);
    } else {
      const int width = qMin(1100, qMax(700, area.width() - 120));
      const int height = qMin(900, qMax(700, area.height() - 80));

      launchpad_->resize(width, height);
      launchpad_->move(
          area.x() + (area.width() - width) / 2,
          area.y() + (area.height() - height) / 2);
    }
  }

  launchpadSearch_->clear();
  launchpadSearch_->setFocus();

  launchpad_->show();

  // Re-apply geometry after Wayland creates the window.
  // Fullscreen mode fills the entire screen. Windowed mode is re-centered.
  if (QScreen* finalScreen = parent_->screen()) {
    if (model_->fullScreenApplicationLauncher()) {
      launchpad_->setGeometry(finalScreen->geometry());
    } else {
      const QRect area = finalScreen->availableGeometry();
      const QSize size = launchpad_->size();

      launchpad_->move(
          area.x() + (area.width() - size.width()) / 2,
          area.y() + (area.height() - size.height()) / 2);
    }
  }

  // Ask KWin to blur everything behind the translucent Launchpad.
  launchpad_->winId();
  KWindowEffects::enableBlurBehind(launchpad_->windowHandle(), true);

  launchpad_->raise();
  launchpad_->activateWindow();
}

void ApplicationMenu::rebuildLaunchpad(const QString& filter) {
  if (!launchpadGrid_) {
    return;
  }

  while (QLayoutItem* item = launchpadGrid_->takeAt(0)) {
    if (item->widget()) {
      item->widget()->deleteLater();
    }
    delete item;
  }

  int row = 0;
  int column = 0;

  const auto addCategories =
      [this, &filter, &row, &column](const std::vector<Category>& categories) {
        for (const auto& category : categories) {
          for (const auto& entry : category.entries) {
            if (entry.hidden) {
              continue;
            }

            if (!filter.trimmed().isEmpty()) {
              const QString text = filter.trimmed();

              if (!entry.name.contains(text, Qt::CaseInsensitive) &&
                  !entry.genericName.contains(text, Qt::CaseInsensitive)) {
                continue;
              }
            }

            addLaunchpadEntry(entry, row, column);
          }
        }
      };

  addCategories(model_->applicationMenuCategories());
  addCategories(model_->applicationMenuSystemCategories());
}

void ApplicationMenu::addLaunchpadEntry(
    const ApplicationEntry& entry,
    int& row,
    int& column) {

  // Windowed Launchpad keeps the original 7-column layout.
  // Fullscreen uses the extra monitor space for a larger app grid.
  const int kColumns =
      model_->fullScreenApplicationLauncher() ? 10 : 7;

  // Each application gets its own tile. The icon and text are separate
  // widgets so Qt cannot crop the icon while trying to fit the label.
  auto* tile = new QWidget(launchpadGridWidget_);
  tile->setFixedSize(132, 140);
  tile->setStyleSheet("background: transparent;");

  auto* tileLayout = new QVBoxLayout(tile);
  tileLayout->setContentsMargins(6, 6, 6, 4);
  tileLayout->setSpacing(5);
  tileLayout->setAlignment(Qt::AlignHCenter | Qt::AlignTop);

  auto* button = new QPushButton(tile);
  button->setCursor(Qt::PointingHandCursor);
  button->setFixedSize(88, 88);
  button->setToolTip(entry.genericName);

  QIcon icon = loadIcon(entry.icon);

  if (icon.isNull()) {
    icon = QIcon::fromTheme("application-x-executable");
  }

  button->setIcon(icon);
  button->setIconSize(QSize(72, 72));

  button->setStyleSheet(
      "QPushButton {"
      " background: transparent;"
      " border: none;"
      " border-radius: 18px;"
      " padding: 6px;"
      "}"
      "QPushButton:hover {"
      " background: rgba(255,255,255,28);"
      "}"
      "QPushButton:pressed {"
      " background: rgba(255,255,255,48);"
      "}");

  auto* label = new QLabel(entry.name, tile);
  label->setAlignment(Qt::AlignHCenter | Qt::AlignTop);
  label->setWordWrap(true);
  label->setFixedWidth(126);
  label->setMaximumHeight(38);
  label->setStyleSheet(
      "QLabel {"
      " color: white;"
      " background: transparent;"
      " font-size: 12px;"
      "}");

  tileLayout->addWidget(button, 0, Qt::AlignHCenter);
  tileLayout->addWidget(label, 0, Qt::AlignHCenter);

  connect(button, &QPushButton::clicked,
          this, [this, entry]() {
            launchpad_->hide();

            showingMenu_ = false;
            parent_->setShowingPopup(false);
            parent_->update();

            Program::launch(entry.command);
          });

  launchpadGrid_->addWidget(tile, row, column);

  ++column;

  if (column >= kColumns) {
    column = 0;
    ++row;
  }
}


void ApplicationMenu::createContextMenu() {
  contextMenu_.addSection("Application Menu");
  contextMenu_.addAction(QIcon::fromTheme("configure"),
                         QString("Application Menu &Settings"),
                         parent_,
                         [this] {
                           parent_->minimize();
                           QTimer::singleShot(DockPanel::kExecutionDelayMs, [this]{
                             parent_->showApplicationMenuSettingsDialog();
                           });
                         });
  contextMenu_.addSeparator();
  parent_->addPanelSettings(&contextMenu_);
}

}  // namespace crystaldock
