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
 * Types keys into the input context Qt loads for QT_IM_MODULE, the way the
 * platform plugin hands key events to it, and prints the context in use
 * and what it commits.  Built against Qt 5 and Qt 6; run it in
 * run-session.sh to test the HIME module on Wayland or xcb.
 *
 * Usage: qt{5,6}-im-test KEY...
 *   KEY is a single printable character or one of <space> <enter> <bs>
 *   <esc>, optionally prefixed by S- (Shift) and/or C- (Control).
 *   @wait MS processes events for MS, then prints what the input context
 *   committed meanwhile (notifications).  @sleep MS waits without
 *   processing events.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <QtCore/QElapsedTimer>
#include <QtGui/QGuiApplication>
#include <QtGui/QInputMethodEvent>
#include <QtGui/QKeyEvent>
#include <QtGui/QWindow>
#include <QtGui/private/qguiapplication_p.h>
#include <QtGui/qpa/qplatforminputcontext.h>
#include <QtGui/qpa/qplatformintegration.h>
#include <QtGui/qpa/qwindowsysteminterface.h>

// notify-check.sh: create $HIME_TEST_READY once waiting, so that it starts
// its actions; stop waiting when it creates $HIME_TEST_DONE.
static void signal_ready (void) {
    const char *ready = getenv ("HIME_TEST_READY");
    if (ready) {
        FILE *f = fopen (ready, "w");
        if (f)
            fclose (f);
    }
}

static int test_done (void) {
    const char *done = getenv ("HIME_TEST_DONE");
    return done && access (done, F_OK) == 0;
}

// A text field: accepts input methods and collects what they commit.
class TextWindow : public QWindow {
  public:
    QString commits;

  protected:
    bool event (QEvent *e) override {
        if (e->type () == QEvent::InputMethodQuery) {
            QInputMethodQueryEvent *query = static_cast<QInputMethodQueryEvent *> (e);
            query->setValue (Qt::ImEnabled, true);
            query->setValue (Qt::ImCursorRectangle, QRect (10, 10, 1, 16));
            return true;
        }
        if (e->type () == QEvent::InputMethod) {
            commits += static_cast<QInputMethodEvent *> (e)->commitString ();
            return true;
        }
        return QWindow::event (e);
    }
};

struct Key {
    int qt_key;
    quint32 keysym;
    QString text;
    Qt::KeyboardModifiers modifiers;
    quint32 native_modifiers;  // X core mask layout, as xcb and QtWayland report
};

static bool parse_key (const char *tok, Key *key) {
    Qt::KeyboardModifiers modifiers = Qt::NoModifier;
    quint32 native_modifiers = 0;
    for (;;) {
        if (!strncmp (tok, "S-", 2) && tok[2]) {
            modifiers |= Qt::ShiftModifier;
            native_modifiers |= 1;  // ShiftMask
        } else if (!strncmp (tok, "C-", 2) && tok[2]) {
            modifiers |= Qt::ControlModifier;
            native_modifiers |= 4;  // ControlMask
        } else {
            break;
        }
        tok += 2;
    }

    static const struct {
        const char *name;
        int qt_key;
        quint32 keysym;
        const char *text;
    } named_keys[] = {
        {"<space>", Qt::Key_Space, 0x20, " "},
        {"<enter>", Qt::Key_Return, 0xff0d, "\r"},
        {"<bs>", Qt::Key_Backspace, 0xff08, "\b"},
        {"<esc>", Qt::Key_Escape, 0xff1b, "\x1b"},
    };

    for (size_t i = 0; i < sizeof (named_keys) / sizeof (named_keys[0]); i++) {
        if (!strcmp (tok, named_keys[i].name)) {
            *key = {named_keys[i].qt_key, named_keys[i].keysym, QString::fromLatin1 (named_keys[i].text), modifiers, native_modifiers};
            return true;
        }
    }

    if (strlen (tok) == 1 && tok[0] > ' ' && tok[0] < 127) {
        // Latin-1 keysyms equal their character codes; the keysym is
        // translated for Shift, as the platform plugins report it
        QChar c = QLatin1Char (tok[0]);
        if (modifiers & Qt::ShiftModifier)
            c = c.toUpper ();
        *key = {c.toUpper ().unicode (), (quint32) c.unicode (), QString (c), modifiers, native_modifiers};
        return true;
    }

    return false;
}

int main (int argc, char **argv) {
    QGuiApplication app (argc, argv);

    TextWindow window;
    window.resize (200, 50);
    window.show ();
    // A headless compositor has no keyboard, so the window never gets the
    // keyboard focus on its own: activate it as the platform plugin would.
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
    QWindowSystemInterface::handleFocusWindowChanged (&window, Qt::ActiveWindowFocusReason);
#else
    QWindowSystemInterface::handleWindowActivated (&window, Qt::ActiveWindowFocusReason);
#endif
    for (int i = 0; i < 50 && QGuiApplication::focusWindow () != &window; i++)
        app.processEvents (QEventLoop::AllEvents, 100);

    QPlatformInputContext *context = QGuiApplicationPrivate::platformIntegration ()->inputContext ();
    if (!context) {
        fprintf (stderr, "no input context\n");
        return 1;
    }
    context->setFocusObject (&window);

    for (int i = 1; i < argc; i++) {
        if (!strcmp (argv[i], "@sleep") && i + 1 < argc) {
            signal_ready ();
            usleep (atoi (argv[++i]) * 1000);
            continue;
        }

        if (!strcmp (argv[i], "@wait") && i + 1 < argc) {
            window.commits.clear ();
            signal_ready ();
            QElapsedTimer timer;
            timer.start ();
            const int ms = atoi (argv[++i]);
            while (timer.elapsed () < ms && !test_done ())
                app.processEvents (QEventLoop::AllEvents, 10);
            app.processEvents (QEventLoop::AllEvents, 10);
            printf ("@wait    commit=\"%s\"\n", window.commits.toUtf8 ().constData ());
            continue;
        }

        Key key;
        if (!parse_key (argv[i], &key)) {
            fprintf (stderr, "bad key: %s\n", argv[i]);
            return 2;
        }

        window.commits.clear ();
        QKeyEvent press (QEvent::KeyPress, key.qt_key, key.modifiers, 0, key.keysym, key.native_modifiers, key.text);
        const bool eaten = context->filterEvent (&press);
        QKeyEvent release (QEvent::KeyRelease, key.qt_key, key.modifiers, 0, key.keysym, key.native_modifiers, key.text);
        context->filterEvent (&release);

        printf ("%-8s %s", argv[i], eaten ? "eat " : "pass");
        if (!window.commits.isEmpty ())
            printf (" commit=\"%s\"", window.commits.toUtf8 ().constData ());
        printf ("\n");
    }

    printf ("module=%s\n", context->metaObject ()->className ());
    printf ("platform=%s\n", QGuiApplication::platformName ().toUtf8 ().constData ());
    printf ("focus=%s\n", QGuiApplication::focusWindow () == &window ? "yes" : "no");
    return 0;
}
