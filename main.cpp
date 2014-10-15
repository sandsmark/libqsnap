#include "oaes_lib.h"
#include "testwidget.h"
#include <QApplication>

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);

    TestWidget widget;
    widget.show();

    app.exec();
}
