#include "OpenNetWorkVideoWindow.h"
#include "ui_OpenNetWorkVideoWindow.h"

OpenNetWorkVideoWindow::OpenNetWorkVideoWindow(QWidget *parent) :
    QMainWindow(parent),
    ui(new Ui::OpenNetWorkVideoWindow)
{
    ui->setupUi(this);

    this->setWindowTitle("打开网络Url");

    this->setMinimumSize(620, 170);

    setStyleSheet(R"(
        QMainWindow {
            background: #f7f8fa;
        }
        QLabel {
            color: #2b3441;
        }
        QLineEdit {
            border: 1px solid #c8d1df;
            border-radius: 5px;
            min-height: 30px;
            padding: 7px 9px;
            selection-background-color: #4aa3ff;
        }
        QPushButton {
            background: #26313f;
            border: 1px solid #3b4858;
            border-radius: 5px;
            color: #ffffff;
            min-height: 30px;
            min-width: 72px;
            padding: 7px 12px;
        }
        QPushButton:hover {
            background: #314052;
        }
        QPushButton:pressed {
            background: #1f2833;
        }
    )");

    ui->lineEdit->setText("rtsp://192.168.3.111:8554/live");
}

OpenNetWorkVideoWindow::~OpenNetWorkVideoWindow()
{
    delete ui;
}

void OpenNetWorkVideoWindow::on_play_btn_clicked()
{
    QString url = ui->lineEdit->text();
    if(!url.isEmpty())
    {
        emit openUrl(url);
    }
}

void OpenNetWorkVideoWindow::on_cancel_btn_clicked()
{
    close();
}
