#include "OpenNetWorkVideoWindow.h"
#include "ui_OpenNetWorkVideoWindow.h"

OpenNetWorkVideoWindow::OpenNetWorkVideoWindow(QWidget *parent) :
    QMainWindow(parent),
    ui(new Ui::OpenNetWorkVideoWindow)
{
    ui->setupUi(this);

    this->setWindowTitle("打开网络Url");

    this->setMinimumWidth(550);

    this->setFixedHeight(100);

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
