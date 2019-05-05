/*
Launchy: Application Launcher
Copyright (C) 2007-2009  Josh Karlin, Simon Capewell

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

#ifndef CHARLINEEDIT_H
#define CHARLINEEDIT_H

#include <QLineEdit>

namespace launchy {
class CharLineEdit : public QLineEdit {
    Q_OBJECT
public:
    CharLineEdit(QWidget* parent = nullptr);

    void processKey(QKeyEvent* event);
    bool focusNextPrevChild(bool next);

    QString separatorText() const;

protected:
    virtual void keyPressEvent(QKeyEvent* event);
    virtual void focusInEvent(QFocusEvent* event);
    virtual void focusOutEvent(QFocusEvent* event);
    virtual void inputMethodEvent(QInputMethodEvent *event);

signals:
    void keyPressed(QKeyEvent* event);
    void focusIn();
    void focusOut();
    void inputMethod(QInputMethodEvent *event);

private:
    bool isAtStartOfSeparator() const;
    bool isAtEndOfSeparator() const;
    QChar separatorChar() const;
};
}

#endif
