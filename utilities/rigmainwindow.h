#ifndef RIGMAINWINDOW_H
#define RIGMAINWINDOW_H
 
#include <kparts/mainwindow.h>  
 
class RigMainWindow : public KParts::MainWindow
{
    Q_OBJECT
public:
    RigMainWindow();
    virtual ~RigMainWindow();
 
public slots:
    /**
     * Use this method to load whatever file/URL you have
     */
    void load(const KUrl& url);
 
    /**
     * Use this method to display an openUrl dialog and
     * load the URL that gets entered
     */
    void load();
 
private:
    void setupActions();
 
    KParts::ReadOnlyPart *m_part;
};
 
#endif // RIGMAINWINDOW_H
