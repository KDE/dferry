#ifndef MAINWIDGET_H
#define MAINWIDGET_H

#include <QWidget>

#include "ui_mainwidget.h"

class MessageSortFilter;

class MainWidget : public QWidget
{
Q_OBJECT
public:
    MainWidget();

private slots:
    void setGrouping(bool enable);

private:
    Ui::MainWidget m_ui;
    MessageSortFilter *m_sortFilter;
};

#endif // MAINWIDGET_H
