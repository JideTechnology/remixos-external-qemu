/* Copyright (C) 2015-2016 The Android Open Source Project
 **
 ** This software is licensed under the terms of the GNU General Public
 ** License version 2, as published by the Free Software Foundation, and
 ** may be copied, distributed, and modified under those terms.
 **
 ** This program is distributed in the hope that it will be useful,
 ** but WITHOUT ANY WARRANTY; without even the implied warranty of
 ** MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 ** GNU General Public License for more details.
 */

#pragma once

#include <QtCore>
#include <QApplication>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QFrame>
#include <QImage>
#include <QMessageBox>
#include <QMouseEvent>
#include <QMoveEvent>
#include <QObject>
#include <QPainter>
#include <QProcess>
#include <QResizeEvent>
#include <QWidget>

#include "android/globals.h"
#include "android/skin/event.h"
#include "android/skin/surface.h"
#include "android/skin/winsys.h"
#include "android/skin/qt/emulator-container.h"
#include "android/skin/qt/emulator-overlay.h"
#include "android/skin/qt/error-dialog.h"
#include "android/skin/qt/tool-window.h"
#include "android/skin/qt/user-actions-counter.h"

#include <memory>

namespace Ui {
    class EmulatorWindow;
}

typedef struct SkinSurface SkinSurface;

class MainLoopThread : public QThread
{
    Q_OBJECT

public:
    MainLoopThread(StartFunction f, int argc, char **argv) : start_function(f), argc(argc), argv(argv) {}
    void run() Q_DECL_OVERRIDE { if (start_function) start_function(argc, argv); }
private:
    StartFunction start_function;
    int argc;
    char **argv;
};

class EmulatorQtWindow final : public QFrame
{
    Q_OBJECT

public:
    using Ptr = std::shared_ptr<EmulatorQtWindow>;

private:
    explicit EmulatorQtWindow(QWidget *parent = 0);

public:
    static void create();
    static EmulatorQtWindow* getInstance();
    static Ptr getInstancePtr();

    virtual ~EmulatorQtWindow();

    void queueQuitEvent();
    void closeEvent(QCloseEvent* event) override;
    void dragEnterEvent(QDragEnterEvent* event) override;
    void dropEvent(QDropEvent* event) override;
    void keyPressEvent(QKeyEvent* event) override;
    void keyReleaseEvent(QKeyEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void paintEvent(QPaintEvent* event) override;
    void startThread(StartFunction f, int argc, char **argv);

    /*
     In Qt, signals are normally events of interest that a class can emit, which can be hooked up to arbitrary slots. Here
     we use this mechanism for a different purpose: it's to allow the QEMU thread to request an operation be performed on
     the Qt thread; Qt allows signals to be emitted from any thread. When used in this fashion, the signal is queued and
     handled asyncrhonously. Since we sometimes will call these signals from Qt's thread as well, we can't use
     BlockingQueuedConnections for these signals, since this connection type will deadlock if called from the same thread.
     For that reason, we use a normal non-blocking connection type, and allow all of the signals to pass an optional semaphore
     that will be released by the slot when it is done processing. If you want to block on the completion of the signal, simply
     pass in the semaphore to the signal and acquire it after the call returns. If you're passing in pointers to data structures
     that could change or go away, you will need to make sure you block to maintain the integrity of the data while the signal runs.

     TODO: allow nonblocking calls to these signals by having the signal take ownership of object pointers. This would allow QEMU
     to do things like update the screen without blocking, which would make it run faster.
     */
signals:
    void blit(QImage *src, QRect *srcRect, QImage *dst, QPoint *dstPos, QPainter::CompositionMode *op, QSemaphore *semaphore = NULL);
    void createBitmap(SkinSurface *s, int w, int h, QSemaphore *semaphore = NULL);
    void fill(SkinSurface *s, const QRect *rect, const QColor *color, QSemaphore *semaphore = NULL);
    void getBitmapInfo(SkinSurface *s, SkinSurfacePixels *pix, QSemaphore *semaphore = NULL);
    void getDevicePixelRatio(double *out_dpr, QSemaphore *semaphore = NULL);
    void getMonitorDpi(int *out_dpi, QSemaphore *semaphore = NULL);
    void getScreenDimensions(QRect *out_rect, QSemaphore *semaphore = NULL);
    void getWindowId(WId *out_id, QSemaphore *semaphore = NULL);
    void getWindowPos(int *x, int *y, QSemaphore *semaphore = NULL);
    void isWindowFullyVisible(bool *out_value, QSemaphore *semaphore = NULL);
    void pollEvent(SkinEvent *event, bool *hasEvent, QSemaphore *semaphore = NULL);
    void queueEvent(SkinEvent *event, QSemaphore *semaphore = NULL);
    void releaseBitmap(SkinSurface *s, QSemaphore *sempahore = NULL);
    void requestClose(QSemaphore *semaphore = NULL);
    void requestUpdate(const QRect *rect, QSemaphore *semaphore = NULL);
    void setWindowIcon(const unsigned char *data, int size, QSemaphore *semaphore = NULL);
    void setWindowPos(int x, int y, QSemaphore *semaphore = NULL);
    void setTitle(const QString *title, QSemaphore *semaphore = NULL);
    void showWindow(SkinSurface* surface, const QRect* rect, int is_fullscreen, QSemaphore *semaphore = NULL);

    // Qt doesn't support function pointers in signals/slots natively, but
    // pointer to function pointer works fine
    void runOnUiThread(SkinGenericFunction* f, void* data, QSemaphore* semaphore = NULL);

public:
    bool isInZoomMode() const;
    ToolWindow* toolWindow() const;
    QSize containerSize() const;

    void doResize(const QSize& size,
                  bool isKbdShortcut = false,
                  bool flipDimensions = false);
    void handleMouseEvent(SkinEventType type,
                          SkinMouseButtonType button,
                          const QPoint& pos);
    void panHorizontal(bool left);
    void panVertical(bool up);
    void recenterFocusPoint();
    void saveZoomPoints(const QPoint &focus, const QPoint &viewportFocus);
    void scaleDown();
    void scaleUp();
    void screenshot();
    void setOnTop(bool onTop);
    void simulateKeyPress(int keyCode, int modifiers);
    void simulateScrollBarChanged(int x, int y);
    void simulateSetScale(double scale);
    void simulateSetZoom(double zoom);
    void simulateWindowMoved(const QPoint &pos);
    void simulateZoomedWindowResized(const QSize &size);
    void toggleZoomMode();
    void zoomIn();
    void zoomIn(const QPoint &focus, const QPoint &viewportFocus);
    void zoomOut();
    void zoomOut(const QPoint &focus, const QPoint &viewportFocus);
    void zoomReset();
    void zoomTo(const QPoint &focus, const QSize &rectSize);

private slots:
    void slot_blit(QImage *src, QRect *srcRect, QImage *dst, QPoint *dstPos, QPainter::CompositionMode *op, QSemaphore *semaphore = NULL);
    void slot_clearInstance();
    void slot_createBitmap(SkinSurface *s, int w, int h, QSemaphore *semaphore = NULL);
    void slot_fill(SkinSurface *s, const QRect *rect, const QColor *color, QSemaphore *semaphore = NULL);
    void slot_getBitmapInfo(SkinSurface *s, SkinSurfacePixels *pix, QSemaphore *semaphore = NULL);
    void slot_getDevicePixelRatio(double *out_dpr, QSemaphore *semaphore = NULL);
    void slot_getMonitorDpi(int *out_dpi, QSemaphore *semaphore = NULL);
    void slot_getScreenDimensions(QRect *out_rect, QSemaphore *semaphore = NULL);
    void slot_getWindowId(WId *out_id, QSemaphore *semaphore = NULL);
    void slot_getWindowPos(int *x, int *y, QSemaphore *semaphore = NULL);
    void slot_isWindowFullyVisible(bool *out_value, QSemaphore *semaphore = NULL);
    void slot_pollEvent(SkinEvent *event, bool *hasEvent, QSemaphore *semaphore = NULL);
    void slot_queueEvent(SkinEvent *event, QSemaphore *semaphore = NULL);
    void slot_releaseBitmap(SkinSurface *s, QSemaphore *sempahore = NULL);
    void slot_requestClose(QSemaphore *semaphore = NULL);
    void slot_requestUpdate(const QRect *rect, QSemaphore *semaphore = NULL);
    void slot_setWindowIcon(const unsigned char *data, int size, QSemaphore *semaphore = NULL);
    void slot_setWindowPos(int x, int y, QSemaphore *semaphore = NULL);
    void slot_setWindowTitle(const QString *title, QSemaphore *semaphore = NULL);
    void slot_showWindow(SkinSurface* surface, const QRect* rect, int is_fullscreen, QSemaphore *semaphore = NULL);
    void slot_runOnUiThread(SkinGenericFunction* f, void* data, QSemaphore* semaphore = NULL);

    void slot_horizontalScrollChanged(int value);
    void slot_verticalScrollChanged(int value);

    void slot_scrollRangeChanged(int min, int max);

    void slot_startupTick();

    void slot_avdArchWarningMessageAccepted();
    void slot_gpuWarningMessageAccepted();

    void slot_showProcessErrorDialog(QProcess::ProcessError exitStatus);
    void wheelEvent(QWheelEvent* event);
    void wheelScrollTimeout();

    /*
     Here are conventional slots that perform interesting high-level functions in the emulator. These can be hooked up to signals
     from UI elements or called independently.
     */
public slots:
    void slot_screencapFinished(int exitStatus);
    void slot_screencapPullFinished(int exitStatus);

    void activateWindow();
    void raise();
    void setForwardShortcutsToDevice(int index);
    void show();
    void showMinimized();

    void slot_screenChanged();

private:

    // When the main window appears, close the "Starting..."
    // pop-up, if it was displayed.
    void showEvent(QShowEvent* event) {
        mStartupTimer.stop();
        mStartupDialog.close();
    }

    void showAvdArchWarning();
    void showGpuWarning();

    bool mouseInside();
    SkinMouseButtonType getSkinMouseButton(QMouseEvent *event) const;

    SkinEvent *createSkinEvent(SkinEventType type);
    void forwardKeyEventToEmulator(SkinEventType type, QKeyEvent* event);
    void handleKeyEvent(SkinEventType type, QKeyEvent *event);
    QString getTmpImagePath();
    void setFrameOnTop(QFrame* frame, bool onTop);

    void* mBatteryState;

    QTimer          mStartupTimer;
    QProgressDialog mStartupDialog;

    SkinSurface* mBackingSurface;
    QQueue<SkinEvent*> mSkinEventQueue;
    ToolWindow* mToolWindow;
    EmulatorContainer mContainer;
    EmulatorOverlay mOverlay;

    QPointF mFocus;
    QPoint mViewportFocus;
    double mZoomFactor;
    bool mInZoomMode;
    bool mNextIsZoom;
    bool mForwardShortcutsToDevice;
    QPoint mPrevMousePosition;

    QProcess mScreencapProcess;
    QProcess mScreencapPullProcess;
    MainLoopThread *mMainLoopThread;

    QMessageBox mAvdWarningBox;
    QMessageBox mGpuWarningBox;
    bool mFirstShowEvent;

    QTimer mWheelScrollTimer;
    EventCapturer mEventCapturer;
    std::shared_ptr<android::qt::UserActionsCounter> mUserActionsCounter;
    bool mMouseGrabbed;
    bool mMouseGrabWarning;
    void grabMouseIfNeccessary(QMouseEvent*);
};

struct SkinSurface {
    int refcount;
    int id;
    QImage *bitmap;
    int w, h, original_w, original_h;
    EmulatorQtWindow::Ptr window;
};
