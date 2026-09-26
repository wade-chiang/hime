/*
 * Copyright (C) 2026 The HIME team, Taiwan
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation version 2.1
 * of the License.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301  USA
 */

/*
 * The Qt 6 counterpart of text-input-test.c: a QLineEdit using Qt's own
 * Wayland text-input support (QT_IM_MODULE=wayland), typed into with real
 * key events from wl-type.
 *
 * Usage: qt6-text-input-test TOKEN...
 *   TOKEN is a key for wl-type (see wl-type.c), or @check to print the
 *   line's text and preedit.  They are printed once more at the end.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <QtCore/QFileInfo>
#include <QtCore/QProcess>
#include <QtCore/QTimer>
#include <QtGui/QInputMethodEvent>
#include <QtWidgets/QApplication>
#include <QtWidgets/QLineEdit>

class LineEdit : public QLineEdit {
  public:
    QString preedit;

  protected:
    void inputMethodEvent (QInputMethodEvent *event) override {
        preedit = event->preeditString ();
        QLineEdit::inputMethodEvent (event);
    }
};

static QStringList tokens;
static QString dir;
static LineEdit *edit;

static void print_state (void) {
    printf ("text=\"%s\" preedit=\"%s\"\n", edit->text ().toUtf8 ().constData (),
            edit->preedit.toUtf8 ().constData ());
    fflush (stdout);
}

// Type the keys up to the next @check, or print the state
static void run_tokens (void) {
    if (tokens.isEmpty ()) {
        print_state ();
        QCoreApplication::quit ();
        return;
    }
    if (tokens.first () == "@check") {
        tokens.removeFirst ();
        print_state ();
        run_tokens ();
        return;
    }

    QStringList keys;
    while (!tokens.isEmpty () && tokens.first () != "@check") {
        keys << tokens.takeFirst ();
    }
    QProcess *process = new QProcess ();
    process->setProcessChannelMode (QProcess::ForwardedChannels);
    QObject::connect (process, &QProcess::finished, [process] () {
        process->deleteLater ();
        // let the input method's commits arrive
        QTimer::singleShot (300, run_tokens);
    });
    process->start (dir + "/wl-type", keys);
}

int main (int argc, char **argv) {
    // Qt's text-input support, whatever the session set up
    qputenv ("QT_IM_MODULE", "wayland");
    qunsetenv ("QT_IM_MODULES");
    qputenv ("QT_QPA_PLATFORM", "wayland");

    QApplication app (argc, argv);
    for (int i = 1; i < argc; i++) {
        tokens << QString::fromUtf8 (argv[i]);
    }
    dir = QFileInfo (QString::fromUtf8 (argv[0])).absolutePath ();

    edit = new LineEdit ();
    edit->show ();
    edit->setFocus ();

    // Once the window has the keyboard focus, the line enables text input
    // and the compositor activates the input method: give that a moment.
    bool started = false;
    QObject::connect (&app, &QGuiApplication::focusWindowChanged, [&started] (QWindow *window) {
        if (window && !started) {
            started = true;
            QTimer::singleShot (500, run_tokens);
        }
    });
    QTimer::singleShot (30000, [] () {
        fprintf (stderr, "qt-text-input-test: timed out\n");
        print_state ();
        exit (1);
    });

    return app.exec ();
}
