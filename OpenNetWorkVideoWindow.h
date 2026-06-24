#ifndef OPENNETWORKVIDEOWINDOW_H
#define OPENNETWORKVIDEOWINDOW_H

#include <QObject>
#include <QMainWindow>

namespace Ui {
class OpenNetWorkVideoWindow;
}
// 输入网络视频地址并通知主窗口开始播放
class OpenNetWorkVideoWindow : public QMainWindow
{
    Q_OBJECT

public:
    explicit OpenNetWorkVideoWindow(QWidget *parent = nullptr);
    ~OpenNetWorkVideoWindow();

signals:
    void openUrl(QString url);

private slots:
    void on_play_btn_clicked();

    void on_cancel_btn_clicked();

private:
    Ui::OpenNetWorkVideoWindow *ui;
};

#endif // OPENNETWORKVIDEOWINDOW_H
