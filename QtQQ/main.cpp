#include "UserLogin.h"

#include <QtWidgets/QApplication>


int main(int argc, char *argv[])
{
    QApplication app(argc, argv);
    app.setQuitOnLastWindowClosed(false);

    UserLogin* userLogin = new UserLogin;
    userLogin->show();


    return app.exec();
}
