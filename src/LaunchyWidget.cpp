/*
Launchy: Application Launcher
Copyright (C) 2007-2010  Josh Karlin, Simon Capewell

This program is free software; you can redistribute it and/or
modify it under the terms of the GNU General Public License
as published by the Free Software Foundation; either version 2
of the License, or (at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program; if not, write to the Free Software
Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.
*/

#include "LaunchyWidget.h"
#include <QScrollBar>
#include <QMessageBox>
#include <QDesktopWidget>
#include <QMenu>
#include <QSystemTrayIcon>
#include <QPushButton>
#include "QHotkey/QHotkey.h"
#include "GlobalVar.h"
#include "IconDelegate.h"
#include "OptionDialog.h"
#include "OptionItem.h"
#include "FileSearch.h"
#include "SettingsManager.h"
#include "AppBase.h"
#include "Fader.h"
#include "IconDelegate.h"
#include "AnimationLabel.h"
#include "CharListWidget.h"
#include "CharLineEdit.h"
#include "Catalog.h"
#include "CatalogBuilder.h"
#include "PluginInterface.h"
#include "PluginHandler.h"
#include "PluginMsg.h"
#include "UpdateChecker.h"

#include "TestWidget.h"

namespace launchy {

// for qt flags
// check this page https://stackoverflow.com/questions/10755058/qflags-enum-type-conversion-fails-all-of-a-sudden
using ::operator|;

LaunchyWidget* LaunchyWidget::s_instance;

LaunchyWidget::LaunchyWidget(CommandFlags command)
#if defined(Q_OS_WIN) || defined(Q_OS_LINUX)
    : QWidget(NULL, Qt::FramelessWindowHint | Qt::Tool),
#elif defined(Q_OS_MAC)
    : QWidget(NULL, Qt::FramelessWindowHint),
#endif
      m_skinChanged(false),
      m_inputBox(new CharLineEdit(this)),
      m_outputBox(new QLabel(this)),
      m_outputIcon(new QLabel(this)),
      m_alternativeList(new CharListWidget(this)),
      m_optionButton(new QPushButton(this)),
      m_closeButton(new QPushButton(this)),
      m_workingAnimation(new AnimationLabel(this)),
      m_trayIcon(new QSystemTrayIcon(this)),
      m_fader(new Fader(this)),
      m_pHotKey(new QHotkey(this)),
      m_rebuildTimer(new QTimer(this)),
      m_dropTimer(new QTimer(this)),
      m_alwaysShowLaunchy(false),
      m_dragging(false),
      m_menuOpen(false),
      m_optionDialog(nullptr),
      m_optionsOpen(false) {

    g_searchText.clear();

    setObjectName("launchy");
    setWindowTitle(tr("Launchy"));

#if defined(Q_OS_WIN)
    setWindowIcon(QIcon(":/resources/launchy128.png"));
#elif defined(Q_OS_MAC)
    setWindowIcon(QIcon("../Resources/launchy_icon_mac.icns"));
    //setAttribute(Qt::WA_MacAlwaysShowToolWindow);
#endif

    setAttribute(Qt::WA_AlwaysShowToolTips);
    setAttribute(Qt::WA_InputMethodEnabled);
    if (g_app->supportsAlphaBorder()) {
        setAttribute(Qt::WA_TranslucentBackground);
    }
    setFocusPolicy(Qt::ClickFocus);

    createActions();

    connect(&m_iconExtractor, SIGNAL(iconExtracted(int, QString, QIcon)),
            this, SLOT(iconExtracted(int, QString, QIcon)));

    m_inputBox->setObjectName("input");
    connect(m_inputBox, SIGNAL(keyPressed(QKeyEvent*)), this, SLOT(onInputBoxKeyPressed(QKeyEvent*)));
    //connect(input, SIGNAL(focusIn()), this, SLOT(onInputFocusIn()));
    connect(m_inputBox, SIGNAL(focusOut()), this, SLOT(onInputBoxFocusOut()));
    connect(m_inputBox, SIGNAL(inputMethod(QInputMethodEvent*)), this, SLOT(onInputBoxInputMethod(QInputMethodEvent*)));
    //connect(m_inputBox, SIGNAL(textEdited(const QString&)),
    //        this, SLOT(onInputBoxTextEdited(const QString&)));

    m_outputBox->setObjectName("output");
    m_outputBox->setAlignment(Qt::AlignHCenter);

    m_outputIcon->setObjectName("outputIcon");
    m_outputIcon->setGeometry(QRect());

    m_alternativeList->setObjectName("alternatives");
    setAlternativeListMode(g_settings->value(OPSTION_CONDENSEDVIEW, OPSTION_CONDENSEDVIEW_DEFAULT).toInt());
    connect(m_alternativeList, SIGNAL(currentRowChanged(int)), this, SLOT(onAlternativeListRowChanged(int)));
    connect(m_alternativeList, SIGNAL(keyPressed(QKeyEvent*)), this, SLOT(onAlternativeListKeyPressed(QKeyEvent*)));
    connect(m_alternativeList, SIGNAL(focusOut()), this, SLOT(onAlternativeListFocusOut()));

    m_optionButton->setObjectName("opsButton");
    m_optionButton->setToolTip(tr("Options"));
    m_optionButton->setGeometry(QRect());
    connect(m_optionButton, SIGNAL(clicked()), this, SLOT(showOptionDialog()));

    m_closeButton->setObjectName("closeButton");
    m_closeButton->setToolTip(tr("Close"));
    m_closeButton->setGeometry(QRect());
    connect(m_closeButton, SIGNAL(clicked()), qApp, SLOT(quit()));

    m_workingAnimation->setObjectName("workingAnimation");
    m_workingAnimation->setGeometry(QRect());

    showTrayIcon();

    connect(m_fader, SIGNAL(fadeLevel(double)), this, SLOT(setFadeLevel(double)));

    // If this is the first time running or a new version, call updateVersion
    int version = g_settings->value(OPSTION_VERSION, OPSTION_VERSION_DEFAULT).toInt();
    if (version != LAUNCHY_VERSION) {
        updateVersion(version);
        command |= ShowLaunchy;
    }

    // Set the general options
    if (setAlwaysShow(g_settings->value(OPSTION_ALWAYSSHOW, OPSTION_ALWAYSSHOW_DEFAULT).toBool())) {
        command |= ShowLaunchy;
    }
    setAlwaysTop(g_settings->value(OPSTION_ALWAYSTOP, OPSTION_ALWAYSTOP_DEFAULT).toBool());

    // Set the hotkey
    QKeySequence hotkey = getHotkey();
    connect(m_pHotKey, &QHotkey::activated, this, &LaunchyWidget::onHotkey);
    if (!setHotkey(hotkey)) {
        QMessageBox::warning(this, tr("Launchy"),
                             tr("The hotkey %1 is already in use, please select another.")
                             .arg(hotkey.toString()));
        command = ShowLaunchy | ShowOptions;
    }

    // Load the catalog
    connect(g_builder, SIGNAL(catalogIncrement(int)), this, SLOT(catalogProgressUpdated(int)));
    connect(g_builder, SIGNAL(catalogFinished()), this, SLOT(catalogBuilt()));

    if (!g_catalog->load(SettingsManager::instance().catalogFilename())) {
        command |= Rescan;
    }

    // Load the history
    m_history.load(SettingsManager::instance().historyFilename());

    // Load fail-safe basic skin
    QFile basicSkinFile(":/resources/basicskin.qss");
    basicSkinFile.open(QFile::ReadOnly);
    qApp->setStyleSheet(basicSkinFile.readAll());
    // Load skin
    applySkin(g_settings->value(OPSTION_SKIN, OPSTION_SKIN_DEFAULT).toString());

    // Move to saved position
    loadPosition(g_settings->value(OPSTION_POS, OPSTION_POS_DEFAULT).toPoint());

    connect(g_app, &SingleApplication::instanceStarted,
            this, &LaunchyWidget::onSecondInstance);

    // Set the timers
    m_dropTimer->setSingleShot(true);
    connect(m_dropTimer, SIGNAL(timeout()), this, SLOT(dropTimeout()));

    m_rebuildTimer->setSingleShot(true);
    connect(m_rebuildTimer, SIGNAL(timeout()), this, SLOT(buildCatalog()));
    startRebuildTimer();

    // start update checker
    UpdateChecker::instance().startup();

    // Load the plugins
    PluginHandler::instance().loadPlugins();

    executeStartupCommand(command);
}

LaunchyWidget::~LaunchyWidget() {
    s_instance = nullptr;
    m_trayIcon->hide();
    if (m_optionDialog) {
        m_optionDialog->close();
        delete m_optionDialog;
        m_optionDialog = nullptr;
    }
}

LaunchyWidget* LaunchyWidget::instance() {
    return s_instance;
}

void LaunchyWidget::cleanup() {
    if (s_instance) {
        delete s_instance;
        s_instance = nullptr;
    }
}

void LaunchyWidget::executeStartupCommand(int command) {
    if (command & ResetPosition) {
        QRect r = geometry();
        int primary = qApp->desktop()->primaryScreen();
        QRect scr = qApp->desktop()->availableGeometry(primary);

        QPoint pt(scr.width()/2 - r.width()/2, scr.height()/2 - r.height()/2);
        move(pt);
    }

    if (command & ResetSkin) {
        setOpaqueness(100);
        showTrayIcon();
        applySkin("Default");
    }

    if (command & ShowLaunchy)
        showLaunchy();

    if (command & ShowOptions)
        showOptionDialog();

    if (command & Rescan)
        buildCatalog();

    if (command & Exit)
        exit();
}

void LaunchyWidget::showEvent(QShowEvent* event) {
    if (m_skinChanged) {
        // output icon may changed with skin
        updateOutputSize();
        m_skinChanged = false;
    }
    QWidget::showEvent(event);
}

void LaunchyWidget::paintEvent(QPaintEvent* event) {
    // Do the default draw first to render any background specified in the stylesheet
    QStyleOption styleOption;
    styleOption.init(this);
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);
    style()->drawPrimitive(QStyle::PE_Widget, &styleOption, &painter, this);

    // Now draw the standard frame.png graphic if there is one
    if (!m_frameGraphic.isNull()) {
        painter.drawPixmap(0, 0, m_frameGraphic);
    }

    QWidget::paintEvent(event);
}

void LaunchyWidget::setAlternativeListMode(int mode) {
    m_alternativeList->setListMode(mode);
}

bool LaunchyWidget::setHotkey(const QKeySequence& hotkey) {
    QKeySequence seqOld = m_pHotKey->keySeq();
    m_pHotKey->setKeySeq(hotkey);

    if (!m_pHotKey->registered()) {
        m_pHotKey->setKeySeq(seqOld);
        return false;
    }

    m_trayIcon->setToolTip(tr("Launchy %1\npress %2 to activate")
                           .arg(LAUNCHY_VERSION_STRING)
                           .arg(hotkey.toString()));

    return true;
}

void LaunchyWidget::showTrayIcon() {
    m_trayIcon->setIcon(QIcon(":/resources/launchy16.png"));
    m_trayIcon->show();
    connect(m_trayIcon, SIGNAL(activated(QSystemTrayIcon::ActivationReason)),
            this, SLOT(trayIconActivated(QSystemTrayIcon::ActivationReason)));
    if (!m_trayIcon->contextMenu()) {
        QMenu* trayMenu = new QMenu(this);

#if 0
        QAction* actTest = new QAction(tr("Test Widget"), this);
        connect(actTest, &QAction::triggered, []() {
            pluginpy::TestWidget::instance().initTestWidget();
        });
        trayMenu->addAction(actTest);
        trayMenu->addSeparator();
#endif

        trayMenu->addAction(m_actShow);
        trayMenu->addAction(m_actReloadSkin);
        trayMenu->addAction(m_actRebuild);
        trayMenu->addSeparator();
        trayMenu->addAction(m_actOptions);
        trayMenu->addAction(m_actCheckUpdate);
        trayMenu->addSeparator();
        trayMenu->addAction(m_actRestart);
        trayMenu->addAction(m_actExit);
        m_trayIcon->setContextMenu(trayMenu);
    }
}


// Repopulate the alternatives list with the current search results
// and set its size and position accordingly.
void LaunchyWidget::updateAlternativeList(bool resetSelection) {
    int mode = g_settings->value(OPSTION_CONDENSEDVIEW, OPSTION_CONDENSEDVIEW_DEFAULT).toInt();
    int i = 0;
    for (; i < m_searchResult.size(); ++i) {
        qDebug() << "LaunchyWidget::updateAlternativeList," << i << ":" << m_searchResult[i].fullPath;
        QString fullPath = QDir::toNativeSeparators(m_searchResult[i].fullPath);
#ifdef _DEBUG
        fullPath += QString(" (%1 launches)").arg(m_searchResult[i].usage);
#endif
        QListWidgetItem* item;
        if (i < m_alternativeList->count()) {
            item = m_alternativeList->item(i);
        }
        else {
            item = new QListWidgetItem(fullPath, m_alternativeList);
        }
        if (item->data(mode == 1 ? ROLE_SHORT : ROLE_FULL) != fullPath) {
            // condensedTempIcon is a blank icon or null
            item->setData(ROLE_ICON, QIcon());
            item->setSizeHint(QSize(32, 32));
        }
        item->setData(mode == 1 ? ROLE_FULL : ROLE_SHORT, m_searchResult[i].shortName);
        item->setData(mode == 1 ? ROLE_SHORT : ROLE_FULL, fullPath);
        if (i >= m_alternativeList->count())
            m_alternativeList->addItem(item);
    }

    while (m_alternativeList->count() > i) {
        delete m_alternativeList->takeItem(i);
    }

    if (resetSelection) {
        m_alternativeList->setCurrentRow(0);
    }
    m_iconExtractor.processIcons(m_searchResult);

    m_alternativeList->updateGeometry(pos(), m_inputBox->pos());
}


void LaunchyWidget::showAlternativeList() {
    // Ensure that any pending shows of the alternatives list are cancelled
    // so that we only update the list once.
    m_dropTimer->stop();

    m_alternativeList->show();
    m_alternativeList->setFocus();
}


void LaunchyWidget::hideAlternativeList() {
    // Ensure that any pending shows of the alternatives list are cancelled
    // so that the list isn't erroneously shown shortly after being dismissed.
    m_dropTimer->stop();

    // clear the selection before hiding to prevent flicker
    m_alternativeList->setCurrentRow(-1);
    m_alternativeList->repaint();
    m_alternativeList->hide();
    m_iconExtractor.stop();
}


void LaunchyWidget::launchItem(CatItem& item) {
    int ops = MSG_CONTROL_LAUNCHITEM;

    if (item.pluginId != HASH_LAUNCHY && item.pluginId != HASH_LAUNCHYFILE) {
        ops = PluginHandler::instance().launchItem(&m_inputData, &item);
        switch (ops) {
        case MSG_CONTROL_EXIT:
            exit();
            break;
        case MSG_CONTROL_OPTIONS:
            showOptionDialog();
            break;
        case MSG_CONTROL_REBUILD:
            buildCatalog();
            break;
        case MSG_CONTROL_RELOADSKIN:
            reloadSkin();
            break;
        default:
            break;
        }
    }

    if (ops == MSG_CONTROL_LAUNCHITEM) {
        QString args;
        if (m_inputData.count() > 1) {
            for (int i = 1; i < m_inputData.count(); ++i) {
                args += m_inputData[i].getText() + " ";
            }
        }
        runProgram(item.fullPath, args);
    }

    g_catalog->incrementUsage(item);
    m_history.addItem(m_inputData);
}

/*
void LaunchyWidget::focusInEvent(QFocusEvent* event) {
    if (event->gotFocus() && fader->isFading())
        fader->fadeIn(false);

    QWidget::focusInEvent(event);
}

void LaunchyWidget::focusOutEvent(QFocusEvent* event) {
    Qt::FocusReason reason = event->reason();
    if (event->reason() == Qt::ActiveWindowFocusReason) {
        if (g_settings->value("GenOps/hideiflostfocus", false).toBool()
            && !isActiveWindow()
            && !alternatives->isActiveWindow()
            && !optionsOpen
            && !fader->isFading()) {
            hideLaunchy();
        }
    }
}
*/

void LaunchyWidget::onAlternativeListRowChanged(int index) {
    // Check that index is a valid history item index
    // If the current entry is a history item or there is no text entered
    if (index < 0 || index >= m_searchResult.count()) {
        return;
    }

    const CatItem& item = m_searchResult[index];
    if ((!m_inputData.isEmpty() && m_inputData.first().hasLabel(LABEL_HISTORY))
        || m_inputBox->text().isEmpty()) {
        // Used a void* to hold an int.. ick!
        // BUT! Doing so avoids breaking existing catalogs
        int64_t hi = reinterpret_cast<int64_t>(item.data);
        int historyIndex = static_cast<int>(hi);

        if (item.pluginId == HASH_HISTORY && historyIndex < m_searchResult.count()) {
            m_inputData = m_history.getItem(historyIndex);
            m_inputBox->selectAll();
            m_inputBox->insert(m_inputData.toString());
            m_inputBox->selectAll();
            m_outputBox->setText(m_inputData[0].getTopResult().shortName);
            // No need to fetch the icon again, just grab it from the alternatives row
            m_outputIcon->setPixmap(m_alternativeList->item(index)->icon().pixmap(m_outputIcon->size()));
            m_outputItem = item;
            g_searchText = m_inputData.toString();
        }
    }
    else if (!m_inputData.isEmpty()
             && (m_inputData.last().hasLabel(LABEL_AUTOSUGGEST)
                 || !m_inputData.last().hasText())) {
        qDebug() << "Autosuggest" << item.shortName;

        m_inputData.last().setText(item.shortName);
        m_inputData.last().setLabel(LABEL_AUTOSUGGEST);

        QString inputRoot = m_inputData.toString(true);
        m_inputBox->selectAll();
        m_inputBox->insert(inputRoot + item.shortName);
        m_inputBox->setSelection(inputRoot.length(), item.shortName.length());

        m_outputBox->setText(item.shortName);
        // No need to fetch the icon again, just grab it from the alternatives row
        m_outputIcon->setPixmap(m_alternativeList->item(index)->icon().pixmap(m_outputIcon->size()));
        m_outputItem = item;
        g_searchText = "";
    }
}

void LaunchyWidget::onInputBoxKeyPressed(QKeyEvent* event) {
    // Launchy widget would not receive Key_Tab from inputbox,
    // we have to pass it manually
    if (event->key() == Qt::Key_Tab) {
        qDebug() << "LaunchyWidget::onInputBoxKeyPressed,"
            << "pass event to LaunchyWidget::keyPressEvent";
        keyPressEvent(event);
    }
    else {
        qDebug() << "LaunchyWidget::onInputBoxKeyPressed,"
            << "event ignored";
        event->ignore();
    }
}

void LaunchyWidget::onAlternativeListKeyPressed(QKeyEvent* event) {
    if (event->key() == Qt::Key_Escape) {
        hideAlternativeList();
        m_inputBox->setFocus();
        event->ignore();
    }
    else if (event->key() == Qt::Key_Return
             || event->key() == Qt::Key_Enter
             || event->key() == Qt::Key_Tab) {
        if (!m_searchResult.isEmpty()) {
            int row = m_alternativeList->currentRow();
            if (row > -1) {
                QString location = "History/" + m_inputBox->text();
                QStringList hist;
                hist << m_searchResult[row].shortName << m_searchResult[row].fullPath;
                g_settings->setValue(location, hist);

                if (row > 0)
                    m_searchResult.move(row, 0);

                if (event->key() == Qt::Key_Tab) {
                    doTab();
                    processKey();
                }
                else {
                    // Load up the inputData properly before running the command
                    /* commented out until I find a fix for it breaking the history selection
                    inputData.last().setTopResult(searchResults[0]);
                    doTab();
                    inputData.parse(input->text());
                    inputData.erase(inputData.end() - 1);*/

                    updateOutputBox();
                    keyPressEvent(event);
                }
            }
        }
    }
    else if (event->key() == Qt::Key_Delete
             && (event->modifiers() & Qt::ShiftModifier) != 0) {
        int row = m_alternativeList->currentRow();
        if (row > -1) {
            const CatItem& item = m_searchResult[row];
            if (item.pluginId == HASH_HISTORY) {
                // Delete selected history entry from the alternatives list
                qDebug() << "LaunchyWidget::onAlternativeListKeyPressed,"
                    << "delete history:" << item.shortName;
                m_history.removeAt(row);
                m_inputBox->clear();
                searchOnInput();
                updateAlternativeList(false);
                onAlternativeListRowChanged(m_alternativeList->currentRow());
            }
            else {
                // Demote the selected item down the alternatives list
                qDebug() << "LaunchyWidget::onAlternativeListKeyPressed,"
                    << "demote item:" << item.shortName;
                g_catalog->demoteItem(item);
                searchOnInput();
                updateOutputBox(false);
            }
        }
    }
    else if (event->key() == Qt::Key_Left
             || event->key() == Qt::Key_Right
             || event->text().length() > 0) {
        // Send text entry to the input control
        activateWindow();
        m_inputBox->setFocus();
        event->ignore();
        m_inputBox->processKey(event);
        keyPressEvent(event);
    }
    m_alternativeList->setFocus();
}


void LaunchyWidget::onAlternativeListFocusOut() {
    qDebug() << "LaunchyWidget::onAlternativeFocusOut"
        << "is main widget activeWindow:" << isActiveWindow()
        << "is alternative list active window:" << m_alternativeList->isActiveWindow();
    if (g_settings->value(OPSTION_HIDEIFLOSTFOCUS, OPSTION_HIDEIFLOSTFOCUS_DEFAULT).toBool()
        && !isActiveWindow()
        && !m_alternativeList->isActiveWindow()
        && !m_optionsOpen
        && !m_fader->isFading()) {
        hideLaunchy();
    }
}

void LaunchyWidget::keyPressEvent(QKeyEvent* event) {
    qDebug() << "LaunchyWidget::keyPressEvent,"
        << "key:" << event->key()
        << "modifier:" << event->modifiers()
        << "text:" << event->text();

    if (event->key() == Qt::Key_Escape) {
        if (m_alternativeList->isVisible())
            hideAlternativeList();
        else
            hideLaunchy();
    }

    else if (event->key() == Qt::Key_Return
             || event->key() == Qt::Key_Enter) {
        doEnter();
    }

    else if (event->key() == Qt::Key_Down
             || event->key() == Qt::Key_PageDown
             || event->key() == Qt::Key_Up
             || event->key() == Qt::Key_PageUp) {
        if (m_alternativeList->isVisible()) {
            if (!m_alternativeList->isActiveWindow()) {
                // Don't refactor the activateWindow outside the if, it won't work properly any other way!
                if (m_alternativeList->currentRow() < 0 && m_alternativeList->count() > 0) {
                    m_alternativeList->activateWindow();
                    m_alternativeList->setCurrentRow(0);
                }
                else {
                    m_alternativeList->activateWindow();
                    qApp->sendEvent(m_alternativeList, event);
                }
            }
        }
        else if (event->key() == Qt::Key_Down
                 || event->key() == Qt::Key_PageDown) {
            // do a search and show the results, selecting the first one
            searchOnInput();
            if (m_searchResult.count() > 0) {
                updateAlternativeList();
                showAlternativeList();
            }
        }
    }

    else if ((event->key() == Qt::Key_Tab
              || event->key() == Qt::Key_Backspace)
             && event->modifiers() == Qt::ShiftModifier) {
        doBackTab();
        processKey();
    }

    else if (event->key() == Qt::Key_Tab) {
        doTab();
        processKey();
    }

    else if (event->key() == Qt::Key_Slash
             || event->key() == Qt::Key_Backslash) {
        if (m_inputData.count() > 0 && m_inputData.last().hasLabel(LABEL_FILE) &&
            m_searchResult.count() > 0 && m_searchResult[0].pluginId == HASH_LAUNCHYFILE)
            doTab();
        processKey();
    }

    else if (event->key()== Qt::Key_Insert
             && event->modifiers() == Qt::ShiftModifier) {
        // ensure pasting text with Shift+Insert also parses input
        // longer term parsing should be done using the TextChanged event
        processKey();
    }

    else if (!event->text().isEmpty()){
        processKey();
    }

}

// remove input text back to the previous input section
void LaunchyWidget::doBackTab()
{
    QString text = m_inputBox->text();
    int index = text.lastIndexOf(m_inputBox->separatorText());
    if (index >= 0)
    {
        text.truncate(index+3);
        m_inputBox->selectAll();
        m_inputBox->insert(text);
    }

    else if (text.lastIndexOf(QDir::separator()) >= 0) {
        text.truncate(text.lastIndexOf(QDir::separator())+1);
        m_inputBox->selectAll();
        m_inputBox->insert(text);
    }
    else if (text.lastIndexOf(QChar(' ')) >= 0) {
        text.truncate(text.lastIndexOf(QChar(' '))+1);
        m_inputBox->selectAll();
        m_inputBox->insert(text);
    }

    else if (text.lastIndexOf(QDir::separator()) >= 0) {
        text.truncate(text.lastIndexOf(QDir::separator())+1);
        m_inputBox->selectAll();
        m_inputBox->insert(text);
    }
    else if (text.lastIndexOf(QChar(' ')) >= 0) {
        text.truncate(text.lastIndexOf(QChar(' '))+1);
        m_inputBox->selectAll();
        m_inputBox->insert(text);
    }
    else
    {
        m_inputBox->clear();
    }
}

void LaunchyWidget::doTab() {
    if (m_inputData.count() > 0 && m_searchResult.count() > 0) {
        // If it's an incomplete file or directory, complete it
        QFileInfo info(m_searchResult[0].fullPath);

        if (m_inputData.last().hasLabel(LABEL_FILE) || info.isDir()) {
            QString path;
            if (info.isSymLink())
                path = info.symLinkTarget();
            else
                path = m_searchResult[0].fullPath;

            if (info.isDir() && !path.endsWith(QDir::separator()))
                path += QDir::separator();

            m_inputBox->selectAll();
            m_inputBox->insert(m_inputData.toString(true) + QDir::toNativeSeparators(path));
        }
        else {
            m_inputData.last().setTopResult(m_searchResult[0]);
            m_inputData.last().setText(m_searchResult[0].shortName);
            m_inputBox->selectAll();
            m_inputBox->insert(m_inputData.toString() + m_inputBox->separatorText());
        }
    }
}


void LaunchyWidget::doEnter() {
    hideAlternativeList();

    if ((!m_inputData.isEmpty() && !m_searchResult.isEmpty())
        || m_inputData.count() > 1) {
        CatItem& item = m_inputData[0].getTopResult();
        qDebug() << "LaunchyWidget::doEnter, launching" << item.shortName << ":" << item.fullPath;
        launchItem(item);
        hideLaunchy();
    }
    else {
        qDebug("LaunchyWidget::doEnter, Nothing to launch");
    }
}

// void LaunchyWidget::inputMethodEvent(QInputMethodEvent* event) {
//     processKey();
//     QWidget::inputMethodEvent(event);
// }

void LaunchyWidget::processKey() {
    qDebug() << "LaunchyWidget::processKey, inputbox text:" << m_inputBox->text();
    m_inputData.parse(m_inputBox->text());
    searchOnInput();
    updateOutputBox();

    // If there is no input text, ensure that the alternatives list is hidden
    // otherwise, show it after the user defined delay if it's not currently visible
    if (m_inputBox->text().isEmpty()) {
        hideAlternativeList();
    }
    else if (!m_alternativeList->isVisible()) {
        startDropTimer();
    }
}

void LaunchyWidget::searchOnInput() {
    QString searchText = m_inputData.count() > 0 ? m_inputData.last().getText() : "";
    QString searchTextLower = searchText.toLower();
    g_searchText = searchTextLower;
    m_searchResult.clear();

    if ((!m_inputData.isEmpty() && m_inputData.first().hasLabel(LABEL_HISTORY))
        || m_inputBox->text().isEmpty()) {
        // Add history items exclusively and unsorted so they remain in most recently used order
        qDebug() << "LaunchyWidget::searchOnInput, searching history for" << searchText;
        m_history.search(searchTextLower, m_searchResult);
    }
    else {
        // Search the catalog for matching items
        if (m_inputData.count() == 1) {
            qDebug() << "LaunchyWidget::searchOnInput, searching catalog for" << searchText;
            g_catalog->searchCatalogs(searchTextLower, m_searchResult);
        }

        if (!m_searchResult.isEmpty()) {
            m_inputData.last().setTopResult(m_searchResult[0]);
        }

        // Give plugins a chance to add their own dynamic matches
        // why getLabels first then getResults, why not getResult straightforward
        PluginHandler& pluginHandler = PluginHandler::instance();
        pluginHandler.getLabels(&m_inputData);
        pluginHandler.getResults(&m_inputData, &m_searchResult);

        // Sort the results by match and usage, then promote any that match previously
        // executed commands
        qSort(m_searchResult.begin(), m_searchResult.end(), CatLessRef);
        g_catalog->promoteRecentlyUsedItems(searchTextLower, m_searchResult);

        // Finally, if the search text looks like a file or directory name,
        // add any file or directory matches
        if (searchText.contains(QDir::separator())
            || searchText.startsWith("~")
            || (searchText.size() == 2 && searchText[0].isLetter() && searchText[1] == ':')) {
            FileSearch::search(searchText, m_searchResult, m_inputData);
        }
    }
}


// If there are current results, update the output text and icon
void LaunchyWidget::updateOutputBox(bool resetAlternativesSelection) {
    if (!m_searchResult.isEmpty()
        && (m_inputData.count() > 1 || !m_inputBox->text().isEmpty())) {
        // qDebug() << "Setting output text to" << searchResults[0].shortName;
        QString outputText = Catalog::decorateText(m_searchResult[0].shortName, g_searchText, true);

#ifdef _DEBUG
        outputText += QString(" (%1 launches)").arg(m_searchResult[0].usage);
#endif

        qDebug() << "LaunchyWidget::updateOutputBox,"
            << "setting output box text:" << outputText
            << "usage: " << m_searchResult[0].usage;
        m_outputBox->setText(outputText);

        if (m_outputItem != m_searchResult[0]) {
            m_outputItem = m_searchResult[0];
            m_outputIcon->clear();
            m_iconExtractor.processIcon(m_searchResult[0], true);
        }

        if (m_outputItem.pluginId != HASH_HISTORY) {
            // Did the plugin take control of the input?
            if (m_inputData.last().getID() != 0)
                m_outputItem.pluginId = m_inputData.last().getID();
            m_inputData.last().setTopResult(m_searchResult[0]);
        }

        // Only update the alternatives list if it is visible
        if (m_alternativeList->isVisible()) {
            updateAlternativeList(resetAlternativesSelection);
        }
    }
    else {
        // No results to show, clear the output UI and hide the alternatives list
        m_outputBox->clear();
        m_outputIcon->clear();
        m_outputItem = CatItem();
        hideAlternativeList();
    }
}

void LaunchyWidget::startDropTimer() {
    int delay = g_settings->value(OPSTION_AUTOSUGGESTDELAY, OPSTION_AUTOSUGGESTDELAY_DEFAULT).toInt();
    if (delay > 0) {
        m_dropTimer->start(delay);
    }
    else {
        dropTimeout();
    }
}

void LaunchyWidget::retranslateUi() {
    m_actShow->setText(tr("Show Launchy"));
    m_actReloadSkin->setText(tr("Reload skin"));
    m_actRebuild->setText(tr("Rebuild catalog"));
    m_actOptions->setText(tr("Options"));
    m_actCheckUpdate->setText(tr("Check for updates"));
    m_actRestart->setText(tr("Restart"));
    m_actExit->setText(tr("Exit"));

    m_optionButton->setToolTip(tr("Options"));
    m_closeButton->setToolTip(tr("Close"));

    m_trayIcon->setToolTip(tr("Launchy %1\npress %2 to activate")
                           .arg(LAUNCHY_VERSION_STRING)
                           .arg(m_pHotKey->keySeq().toString()));
}

void LaunchyWidget::updateOutputSize() {
    int maxIconSize = qMax(m_outputIcon->width(), m_outputIcon->height());
    qDebug() << "LaunchyWidget::showEvent, output icon size:" << maxIconSize;
    g_app->setPreferredIconSize(maxIconSize);
    m_alternativeList->setIconSize(maxIconSize);
}

void LaunchyWidget::dropTimeout() {
    // Don't do anything if Launchy has been hidden since the timer was started
    if (isVisible() && m_searchResult.count() > 0) {
        updateAlternativeList();
        showAlternativeList();
    }
}

void LaunchyWidget::iconExtracted(int itemIndex, QString path, QIcon icon) {
    if (itemIndex == -1) {
        // An index of -1 means update the output icon, check that it is also
        // the same item as was originally requested
        if (path == m_outputItem.fullPath) {
            m_outputIcon->setPixmap(icon.pixmap(m_outputIcon->size()));
        }
    }
    else if (itemIndex < m_alternativeList->count()) {
        // >=0 is an item in the alternatives list
        if (itemIndex < m_searchResult.count()
            && path == m_searchResult[itemIndex].fullPath) {
            QListWidgetItem* listItem = m_alternativeList->item(itemIndex);
            listItem->setIcon(icon);
            listItem->setData(ROLE_ICON, icon);

            QRect rect = m_alternativeList->visualItemRect(listItem);
            repaint(rect);
        }
    }
}

void LaunchyWidget::catalogProgressUpdated(int value) {
    if (value == 0) {
        m_workingAnimation->Start();
    }
}

void LaunchyWidget::catalogBuilt() {
    // Save settings and updated catalog, stop the "working" animation
    saveSettings();
    m_workingAnimation->Stop();

    // Now do a search using the updated catalog
    searchOnInput();
    updateOutputBox();
}

void LaunchyWidget::setSkin(const QString& name) {
    hideLaunchy(true);
    applySkin(name);
    showLaunchy(false);
}

void LaunchyWidget::updateVersion(int oldVersion) {
    if (oldVersion < 199) {
        SettingsManager::instance().removeAll();
        SettingsManager::instance().load();
    }

    if (oldVersion < 249) {
        g_settings->setValue(OPSTION_SKIN, OPSTION_SKIN_DEFAULT);
    }

    if (oldVersion < LAUNCHY_VERSION) {
        g_settings->setValue(OPSTION_VERSION, LAUNCHY_VERSION);
    }
}

void LaunchyWidget::loadPosition(QPoint pt) {
    // Get the dimensions of the screen containing the new center point
    QRect rect = geometry();
    QPoint newCenter = pt + QPoint(rect.width()/2, rect.height()/2);
    QRect screen = qApp->desktop()->availableGeometry(newCenter);

    // See if the new position is within the screen dimensions, if not pull it inside
    if (newCenter.x() < screen.left())
        pt.setX(screen.left());
    else if (newCenter.x() > screen.right())
        pt.setX(screen.right()-rect.width());
    if (newCenter.y() < screen.top())
        pt.setY(screen.top());
    else if (newCenter.y() > screen.bottom())
        pt.setY(screen.bottom()-rect.height());

    int centerOption = g_settings->value(OPSTION_ALWAYSCENTER, OPSTION_ALWAYSCENTER_DEFAULT).toInt();
    if (centerOption & 1)
        pt.setX(screen.center().x() - rect.width()/2);
    if (centerOption & 2)
        pt.setY(screen.center().y() - rect.height()/2);

    move(pt);
}

void LaunchyWidget::savePosition() {
    g_settings->setValue(OPSTION_POS, pos());
}

void LaunchyWidget::saveSettings() {
    qDebug() << "LaunchyWidget::saveSettings";
    savePosition();
    g_settings->sync();
    g_catalog->save(SettingsManager::instance().catalogFilename());
    m_history.save(SettingsManager::instance().historyFilename());
}

void LaunchyWidget::startRebuildTimer() {
    int time = g_settings->value(OPTION_REBUILDTIMER, OPTION_REBUILDTIMER_DEFAULT).toInt();
    if (time > 0) {
        m_rebuildTimer->start(time * 60000);
    }
    else {
        m_rebuildTimer->stop();
    }
}


void LaunchyWidget::trayNotify(const QString& infoMsg) {
    if (m_trayIcon) {
        m_trayIcon->showMessage(tr("Launchy"), infoMsg,
                                QIcon(":/resources/launchy128.png"));
    }
}

void LaunchyWidget::onHotkey() {
    if (m_menuOpen || m_optionsOpen) {
        showLaunchy(true);
        return;
    }
    if (!m_alwaysShowLaunchy
        && isVisible()
        && !m_fader->isFading()
        && QApplication::activeWindow() != nullptr) {
        hideLaunchy();
    }
    else {
        showLaunchy();
    }
}

void LaunchyWidget::closeEvent(QCloseEvent* event) {
    event->ignore();
    hideLaunchy();
}

bool LaunchyWidget::setAlwaysShow(bool alwaysShow) {
    m_alwaysShowLaunchy = alwaysShow;
    return !isVisible() && alwaysShow;
}

bool LaunchyWidget::setAlwaysTop(bool alwaysTop) {
    if (alwaysTop && (windowFlags() & Qt::WindowStaysOnTopHint) == 0) {
        setWindowFlags(windowFlags() | Qt::WindowStaysOnTopHint);
        m_alternativeList->setWindowFlags(windowFlags() | Qt::WindowStaysOnTopHint);
        return true;
    }
    else if (!alwaysTop && (windowFlags() & Qt::WindowStaysOnTopHint) != 0) {
        setWindowFlags(windowFlags() & ~Qt::WindowStaysOnTopHint);
        m_alternativeList->setWindowFlags(windowFlags() & ~Qt::WindowStaysOnTopHint);
        return true;
    }

    return false;
}

void LaunchyWidget::setOpaqueness(int level) {
    double value = level / 100.0;
    setWindowOpacity(value);
    m_alternativeList->setWindowOpacity(value);
}

void LaunchyWidget::reloadSkin() {
    setSkin(m_currentSkin);
}

void LaunchyWidget::exit() {
    m_trayIcon->hide();
    m_fader->stop();
    saveSettings();
    qApp->quit();
}

void LaunchyWidget::onInputBoxFocusOut() {
    if (g_settings->value(OPSTION_HIDEIFLOSTFOCUS, OPSTION_HIDEIFLOSTFOCUS_DEFAULT).toBool()
        && !isActiveWindow()
        && !m_alternativeList->isActiveWindow()
        && !m_optionsOpen
        && !m_fader->isFading()) {
        hideLaunchy();
    }
}

void LaunchyWidget::onInputBoxInputMethod(QInputMethodEvent* event) {
    qDebug() << "LaunchyWidget::onInputBoxInputMethod";
    QString commitStr = event->commitString();
    if (!commitStr.isEmpty()) {
        qDebug() << "LaunchyWidget::onInputBoxInputMethod,"
            << ", commit string:" << commitStr
            << ", inputbox text:" << m_inputBox->text();
        m_inputData.parse(m_inputBox->text());
        searchOnInput();
        updateOutputBox();
    }
}

void LaunchyWidget::onInputBoxTextEdited(const QString& str) {
    qDebug() << "LaunchyWidget::onInputBoxTextEdited, str:" << str;
    processKey();
}

void LaunchyWidget::onSecondInstance() {
    trayNotify(tr("Launchy is already running!"));
}

void LaunchyWidget::applySkin(const QString& name) {
    m_currentSkin = name;
    m_skinChanged = true;

    qDebug() << "apply skin:" << name;

    QString skinPath = SettingsManager::instance().skinPath(name);
    // Use default skin if this one doesn't exist or isn't valid
    if (skinPath.isEmpty()) {
        QString defaultSkin = SettingsManager::instance().directory("defSkin")[0];
        skinPath = SettingsManager::instance().skinPath(defaultSkin);
        // If still no good then fail with an ugly default
        if (skinPath.isEmpty())
            return;

        g_settings->setValue(OPSTION_SKIN, defaultSkin);
    }

    // Set a few defaults
    m_closeButton->setGeometry(QRect());
    m_optionButton->setGeometry(QRect());
    m_inputBox->setAlignment(Qt::AlignLeft);
    m_outputBox->setAlignment(Qt::AlignCenter);
    m_alternativeList->resetGeometry();

    QFile fileStyle(skinPath + "style.qss");
    fileStyle.open(QFile::ReadOnly);
    QString strStyleSheet(fileStyle.readAll());
    // transform stylesheet for external resources
    strStyleSheet.replace("url(", "url("+skinPath);
    qApp->setStyleSheet(strStyleSheet);

    bool validFrame = false;
    QPixmap frame;
    if (g_app->supportsAlphaBorder()) {
        if (frame.load(skinPath + "frame.png")) {
            validFrame = true;
        }
        else if (frame.load(skinPath + "background.png")) {
            QPixmap border;
            if (border.load(skinPath + "mask.png")) {
                frame.setMask(border);
            }
            if (border.load(skinPath + "alpha.png")) {
                QPainter surface(&frame);
                surface.drawPixmap(0, 0, border);
            }
            validFrame = true;
        }
    }

    if (!validFrame) {
        // Set the background image
        if (frame.load(skinPath + "background_nc.png")) {
            validFrame = true;

            // Set the background mask
            QPixmap mask;
            if (mask.load(skinPath + "mask_nc.png")) {
                // For some reason, w/ compiz setmask won't work
                // for rectangular areas. This is due to compiz and
                // XShapeCombineMask
                setMask(mask);
            }
        }
    }

    if (QFile::exists(skinPath + "spinner.gif")) {
        m_workingAnimation->LoadAnimation(skinPath + "spinner.gif");
    }

    if (validFrame) {
        m_frameGraphic.swap(frame);
        resize(m_frameGraphic.size());
    }
    else {
        m_frameGraphic.fill(Qt::transparent);
    }

    // output size may change when skin change
    updateOutputSize();

    // separator may change when skin change
    InputDataList::setSeparator(m_inputBox->separatorText());
}

void LaunchyWidget::mousePressEvent(QMouseEvent *event) {
    if (event->buttons() == Qt::LeftButton) {
        if (!g_settings->value(OPSTION_DRAGMODE, OPSTION_DRAGMODE_DEFAULT).toBool()
            || (event->modifiers() & Qt::ShiftModifier)) {
            m_dragging = true;
            m_dragStartPoint = event->pos();
        }
    }
    hideAlternativeList();
    activateWindow();
    m_inputBox->setFocus();
}

void LaunchyWidget::mouseMoveEvent(QMouseEvent *event) {
    if (event->buttons() == Qt::LeftButton && m_dragging) {
        QPoint pt = event->globalPos() - m_dragStartPoint;
        move(pt);
        hideAlternativeList();
        m_inputBox->setFocus();
    }
}

void LaunchyWidget::mouseReleaseEvent(QMouseEvent* event) {
    Q_UNUSED(event)
    m_dragging = false;
    hideAlternativeList();
    m_inputBox->setFocus();
}

void LaunchyWidget::contextMenuEvent(QContextMenuEvent* event) {
    QMenu menu(this);
    menu.addAction(m_actRebuild);
    menu.addAction(m_actReloadSkin);
    menu.addAction(m_actOptions);
    menu.addSeparator();
    menu.addAction(m_actExit);
    m_menuOpen = true;
    menu.exec(event->globalPos());
    m_menuOpen = false;
}

void LaunchyWidget::changeEvent(QEvent* event) {
    if (event->type() == QEvent::LanguageChange) {
        // retranslate designer form (single inheritance approach)
        retranslateUi();
    }

    // remember to call base class implementation
    QWidget::changeEvent(event);
}

void LaunchyWidget::trayIconActivated(QSystemTrayIcon::ActivationReason reason) {
    switch (reason) {
    case QSystemTrayIcon::Trigger:
        showLaunchy();
        break;
    case QSystemTrayIcon::Unknown:
    case QSystemTrayIcon::Context:
    case QSystemTrayIcon::DoubleClick:
    case QSystemTrayIcon::MiddleClick:
        break;
    }
}

void LaunchyWidget::buildCatalog() {
    m_rebuildTimer->stop();
    saveSettings();

    // Use the catalog builder to refresh the catalog in a worker thread
    QMetaObject::invokeMethod(g_builder, &CatalogBuilder::buildCatalog);

    startRebuildTimer();
}

void LaunchyWidget::showOptionDialog() {
    if (!m_optionsOpen) {
        showLaunchy(true);
        m_optionsOpen = true;

        if (!m_optionDialog) {
            m_optionDialog = new OptionDialog(nullptr);
        }

        m_optionDialog->exec();
        delete m_optionDialog;
        m_optionDialog = nullptr;

        activateWindow();
        m_inputBox->setFocus();
        m_inputBox->selectAll();
        m_optionsOpen = false;
    }
}

void LaunchyWidget::setFadeLevel(double level) {
    level = qMin(level, 1.0);
    level = qMax(level, 0.0);
    setWindowOpacity(level);
    m_alternativeList->setWindowOpacity(level);
    if (level <= 0.001) {
        hide();
    }
    else if (!isVisible()) {
        show();
        raise();
        activateWindow();
        m_inputBox->setFocus();
        m_inputBox->selectAll();
    }
}


void LaunchyWidget::showLaunchy(bool noFade) {

    hideAlternativeList();

    loadPosition(pos());

    m_fader->fadeIn(noFade || m_alwaysShowLaunchy);

#ifdef Q_OS_WIN
    // need to use this method in Windows to ensure that keyboard focus is set when
    // being activated via a hook or message from another instance of Launchy
    // SetForegroundWindowEx((HWND)winId());
#elif defined(Q_OS_LINUX)
    /* Fix for bug 2994680: Not sure why this is necessary, perhaps someone with more
       Qt experience can tell, but doing these two calls will force the window to actually
       get keyboard focus when it is activated. It seems from the bug reports that this
       only affects Linux (and I could only test it on my Linux system - running KDE), so
       it leads me to believe that it is due to an issue in the Qt implementation on Linux. */
    grabKeyboard();
    releaseKeyboard();
#endif
    raise();
    activateWindow();
    m_inputBox->selectAll();
    m_inputBox->setFocus();

    // Let the plugins know
    PluginHandler::instance().showLaunchy();
}

void LaunchyWidget::hideLaunchy(bool noFade) {
    if (!isVisible() || isHidden())
        return;

    savePosition();
    hideAlternativeList();
    if (m_alwaysShowLaunchy)
        return;

    if (isVisible()) {
        m_fader->fadeOut(noFade);
    }

    // let the plugins know
    PluginHandler::instance().hideLaunchy();
}

int LaunchyWidget::getHotkey() const {
    int hotkey = g_settings->value(OPSTION_HOTKEY, -1).toInt();
    if (hotkey == -1) {
        hotkey = g_settings->value(OPSTION_HOTKEYMOD, OPSTION_HOTKEYMOD_DEFAULT).toInt() |
            g_settings->value(OPSTION_HOTKEYKEY, OPSTION_HOTKEYKEY_DEFAULT).toInt();
    }
    return hotkey;
}

void LaunchyWidget::createActions() {
    m_actShow = new QAction(tr("Show Launchy"), this);
    connect(m_actShow, SIGNAL(triggered()), this, SLOT(showLaunchy()));

    m_actReloadSkin = new QAction(tr("Reload skin"), this);
    m_actReloadSkin->setShortcut(QKeySequence(Qt::Key_F5 | Qt::SHIFT));
    connect(m_actReloadSkin, SIGNAL(triggered()), this, SLOT(reloadSkin()));
    addAction(m_actReloadSkin);

    m_actRebuild = new QAction(tr("Rebuild catalog"), this);
    m_actRebuild->setShortcut(QKeySequence(Qt::Key_F5));
    connect(m_actRebuild, SIGNAL(triggered()), this, SLOT(buildCatalog()));
    addAction(m_actRebuild);

    m_actOptions = new QAction(tr("Options"), this);
    m_actOptions->setShortcut(QKeySequence(Qt::Key_Comma | Qt::CTRL));
    connect(m_actOptions, SIGNAL(triggered()), this, SLOT(showOptionDialog()));
    addAction(m_actOptions);

    m_actCheckUpdate = new QAction(tr("Check for updates"), this);
    connect(m_actCheckUpdate, &QAction::triggered, []() {
        UpdateChecker::instance().manualCheck();
    });

    m_actRestart = new QAction(tr("Relaunch"), this);
    connect(m_actRestart, &QAction::triggered, [=]() {
        qInfo() << "Performing application relaunch...";
        // restart:
        //qApp->closeAllWindows();
        m_trayIcon->hide();
        qApp->exit(Restart);
        qInfo() << "Finish application relaunch...";
    });

    m_actExit = new QAction(tr("Exit"), this);
    connect(m_actExit, SIGNAL(triggered()), this, SLOT(exit()));
}
}
