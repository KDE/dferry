#ifndef MAINWIDGET_H
#define MAINWIDGET_H

#include <QWidget>

#include "ui_mainwidget.h"

class EavesdropperModel;
class MessageSortFilter;
class QModelIndex;

class MainWidget : public QWidget
{
Q_OBJECT
public:
    MainWidget();

private slots:
    void setGrouping(bool enable);
    void itemClicked(const QModelIndex &index);

private:
    Ui::MainWidget m_ui;
    EavesdropperModel *m_model;
    MessageSortFilter *m_sortFilter;
};

#endif // MAINWIDGET_H
