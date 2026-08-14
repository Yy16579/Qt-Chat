#include "QtQQ_Server.h"

#include <QtWidgets/QApplication>


int main(int argc, char* argv[])
{
    QApplication app(argc, argv);


    QtQQ_Server serverDialog;
    

    return app.exec();
}