#include "oaes_lib.h"
#include "testwidget.h"
#include <QApplication>
#include <QtCrypto>

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);
    QCA::Initializer init;
    foreach(const QCA::Provider *provider, QCA::providers()) {
        QCA::setProviderPriority(provider->name(), 10);
    }

    QCA::setProviderPriority("qca-ossl", 0);

    foreach(const QCA::Provider *provider, QCA::providers()) {
        qDebug() << provider->name() << QCA::providerPriority(provider->name());
    }
//return 0;
    TestWidget widget;
    widget.show();

    app.exec();
}
