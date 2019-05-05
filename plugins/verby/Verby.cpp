/*
Verby: Plugin for Launchy
Copyright (C) 2009  Simon Capewell

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

#include "Verby.h"
#include "gui.h"
#include "PluginMsg.h"

using namespace launchy;

void Verby::init() {
}

void Verby::setPath(const QString* path) {
    m_libPath = *path;
    qDebug() << "Verby::setPath, m_libPath:" << m_libPath;
}

void Verby::getID(uint* id) {
    *id = HASH_VERBY;
}

void Verby::getName(QString* str) {
    *str = "Verby";
}

QString Verby::getIcon() {
    return m_libPath + "/verby.png";
}

void Verby::getLabels(QList<launchy::InputData>* inputData) {
    if (inputData->count() != 2) {
        return;
    }

    // If it's not an item from Launchy's built in catalog,
    // i.e. a file or directory or something added 
    // by a plugin, don't add verbs.
    if (inputData->first().getID() != 0
        && inputData->first().getID() != HASH_VERBY) {
        return;
    }

    QString path = inputData->first().getTopResult().fullPath;
    QFileInfo info(path);
    if (info.isSymLink()) {
        inputData->first().setLabel(HASH_LINK);
    }
    else if (info.isDir()) {
        inputData->first().setLabel(HASH_DIR);
    }
    else if (info.isExecutable()) {
        inputData->first().setLabel(HASH_EXEC);
    }
    else if (info.isFile()) {
        inputData->first().setLabel(HASH_FILE);
    }
}

const QString& Verby::getIconPath() const {
    return m_libPath;
}

bool Verby::isMatch(const QString& text1, const QString& text2) {
    int text2Length = text2.count();
    int curChar = 0;
    foreach(QChar c, text1) {
        if (c.toLower() == text2[curChar].toLower()) {
            ++curChar;
            if (curChar >= text2Length) {
                return true;
            }
        }
    }
    return false;
}

void Verby::addCatItem(QString text, QList<CatItem>* results,
                       QString fullName, QString shortName, QString iconName) {
    if (text.isEmpty() || isMatch(shortName, text)) {
        CatItem item(fullName, shortName, HASH_VERBY, m_libPath + "/" + iconName);
        item.usage = launchy::g_settings->value("Verby/" + shortName.replace(" ", ""), 0).toInt();
        results->push_back(item);
    }
}

void Verby::updateUsage(CatItem& item) {
    launchy::g_settings->setValue("Verby/" + item.shortName.replace(" ", ""), item.usage + 1);
}


void Verby::getResults(QList<InputData>* inputData, QList<CatItem>* results) {
    if (inputData->count() != 2) {
        return;
    }
    QString text = inputData->at(1).getText();

    if (inputData->first().hasLabel(HASH_DIR)) {
        addCatItem(text, results, "Properties", "Directory properties", "verby_properties.png");
    }
    else if (inputData->first().hasLabel(HASH_EXEC)) {
        addCatItem(text, results, "Run as", "Run as admin", "verby_run.png");
        addCatItem(text, results, "Open containing folder", "Open containing folder", "verby_opencontainer.png");
        addCatItem(text, results, "Copy path", "Copy path to clipboard", "verby_copy.png");
        addCatItem(text, results, "Properties", "File properties", "verby_properties.png");
    }
    else if (inputData->first().hasLabel(HASH_FILE)) {
        addCatItem(text, results, "Open containing folder", "Open containing folder", "verby_opencontainer.png");
        addCatItem(text, results, "Copy path", "Copy path to clipboard", "verby_copy.png");
        addCatItem(text, results, "Properties", "File properties", "verby_properties.png");
    }
    else if (inputData->first().hasLabel(HASH_LINK)) {
        addCatItem(text, results, "Run as", "Run as admin", "verby_run.png");
        addCatItem(text, results, "Open containing folder", "Open containing folder", "verby_opencontainer.png");
        addCatItem(text, results, "Open shortcut folder", "Open shortcut folder", "verby_opencontainer.png");
        addCatItem(text, results, "Copy path", "Copy path to clipboard", "verby_copy.png");
        addCatItem(text, results, "Properties", "File properties", "verby_properties.png");
    }
    else {
        return;
    }

    // Mark the item as a Verby item so that Verby has a chance to process it before Launchy
    inputData->first().setID(HASH_VERBY);
    inputData->first().getTopResult().pluginId = HASH_VERBY;

    // ensure there's always an item at the top of the list for launching with parameters.
    results->push_front(CatItem("Run " + inputData->first().getText(),
                                inputData->last().getText(),
                                UINT_MAX,
                                m_libPath + "/verby_run.png"));

}

int Verby::launchItem(QList<InputData>* inputData, CatItem* item) {
    Q_UNUSED(item)

    if (inputData->count() != 2) {
        // Tell Launchy to handle the command
        return MSG_CONTROL_LAUNCHITEM;
    }

    QString noun = inputData->first().getTopResult().fullPath;
    CatItem& verbItem = inputData->last().getTopResult();
    QString verb = verbItem.shortName;

    qDebug() << "Verby launchItem" << verb;
    if (verb == "Run") {
        runProgram(noun, "");
    }
    else if (verb == "Open containing folder") {
        QFileInfo info(noun);
        if (info.isSymLink()) {
            info.setFile(info.symLinkTarget());
        }

#ifdef Q_OS_WIN
        runProgram("explorer.exe", "\"" + QDir::toNativeSeparators(info.absolutePath()) + "\"");
#endif
    }
    else if (verb == "Open shortcut folder") {
        QFileInfo info(noun);

#ifdef Q_OS_WIN
        runProgram("explorer.exe", "\"" + QDir::toNativeSeparators(info.absolutePath()) + "\"");
#endif
    }
    else if (verb == "Run as admin") {
#ifdef Q_OS_WIN
        SHELLEXECUTEINFO shellExecInfo;

        shellExecInfo.cbSize = sizeof(SHELLEXECUTEINFO);
        shellExecInfo.fMask = SEE_MASK_FLAG_NO_UI;
        shellExecInfo.hwnd = NULL;
        shellExecInfo.lpVerb = L"runas";
        shellExecInfo.lpFile = (LPCTSTR)noun.utf16();
        shellExecInfo.lpParameters = NULL;
        QDir dir(noun);
        QFileInfo info(noun);
        if (!info.isDir() && info.isFile()) {
            dir.cdUp();
        }
        QString filePath = QDir::toNativeSeparators(dir.absolutePath());
        shellExecInfo.lpDirectory = (LPCTSTR)filePath.utf16();
        shellExecInfo.nShow = SW_NORMAL;
        shellExecInfo.hInstApp = NULL;

        ShellExecuteEx(&shellExecInfo);
#endif
    }
    else if (verb == "File properties") {
#ifdef Q_OS_WIN
        SHELLEXECUTEINFO shellExecInfo;

        shellExecInfo.cbSize = sizeof(SHELLEXECUTEINFO);
        shellExecInfo.fMask = SEE_MASK_FLAG_NO_UI | SEE_MASK_INVOKEIDLIST;
        shellExecInfo.hwnd = NULL;
        shellExecInfo.lpVerb = L"properties";
        QString filePath = QDir::toNativeSeparators(noun);
        shellExecInfo.lpFile = (LPCTSTR)filePath.utf16();
        shellExecInfo.lpIDList = NULL;
        shellExecInfo.lpParameters = NULL;
        shellExecInfo.lpDirectory = NULL;
        shellExecInfo.nShow = SW_NORMAL;
        shellExecInfo.hInstApp = NULL;

        ShellExecuteEx(&shellExecInfo);
#endif
    }
    else if (verb == "Copy path to clipboard") {
        QFileInfo info(noun);
        if (info.isSymLink()) {
            info.setFile(info.symLinkTarget());
        }
        QClipboard *clipboard = QApplication::clipboard();
        clipboard->setText(QDir::toNativeSeparators(info.canonicalFilePath()));
    }
    else {
        // Tell Launchy to handle the command
        return MSG_CONTROL_LAUNCHITEM;
    }

    updateUsage(verbItem);
    return true;
}


void Verby::doDialog(QWidget* parent, QWidget** newDlg) {
    if (m_gui == NULL) {
        m_gui = new Gui(parent);
        *newDlg = m_gui;
    }
}


void Verby::endDialog(bool accept) {
    if (accept && m_gui) {
        m_gui->writeOptions();
        init();
    }
    if (m_gui != NULL) {
        m_gui->close();
        delete m_gui;
    }

    m_gui = NULL;
}

Verby::Verby()
    : m_gui(NULL),
      HASH_VERBY(qHash(QString("verby"))),
      HASH_DIR(qHash(QString("verbydirectory"))),
      HASH_FILE(qHash(QString("verbyfile"))),
      HASH_LINK(qHash(QString("verbylink"))),
      HASH_EXEC(qHash(QString("verbyexec"))) {
}

Verby::~Verby() {
}

int Verby::msg(int msgId, void* wParam, void* lParam) {
    int handled = 0;
    switch (msgId) {
    case MSG_INIT:
        init();
        handled = true;
        break;
    case MSG_GET_ID:
        getID((uint*)wParam);
        handled = true;
        break;
    case MSG_GET_NAME:
        getName((QString*)wParam);
        handled = true;
        break;
    case MSG_GET_LABELS:
        getLabels((QList<InputData>*) wParam);
        handled = true;
        break;
    case MSG_GET_RESULTS:
        getResults((QList<InputData>*) wParam, (QList<CatItem>*) lParam);
        handled = true;
        break;
    case MSG_LAUNCH_ITEM:
        handled = launchItem((QList<InputData>*) wParam, (CatItem*)lParam);
        break;
    case MSG_HAS_DIALOG:
        handled = true;
        break;
    case MSG_DO_DIALOG:
        doDialog((QWidget*)wParam, (QWidget**)lParam);
        break;
    case MSG_END_DIALOG:
        endDialog(wParam != 0);
        break;
    case MSG_PATH:
        setPath((QString*)wParam);
        break;

    default:
        break;
    }

    return handled;
}
