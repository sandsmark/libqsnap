#include "testwidget.h"
#include <QCryptographicHash>
#include <QHttpMultiPart>
#include <QUrlQuery>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QFile>
#include <QUuid>
#include <QApplication>

#include <QPushButton>

extern "C" {
#include "aes.h"
}

#define SECRET "iEk21fuwZApXlz93750dmW22pw389dPwOk"

#define BLOBKEY "M02cnQ51Ji97vwT4"

#define BASE_URL QStringLiteral("https://feelinsonice-hrd.appspot.com/bq/")
//#define BASE_URL QStringLiteral("http://localhost:8080/")

TestWidget::TestWidget(QWidget *parent) :
    QWidget(parent),
    m_token("m198sOkJEn37DjqZ32lpRu76xmw288xSQ9"),
    m_outputPath("/home/sandsmark/tmp/")
{
    login("fuckIngHell", "f0uckplz");

    QPushButton *button = new QPushButton("QUIT", this);
    button->setFocus();
    button->setDefault(true);
    connect(button, SIGNAL(clicked()), qApp, SLOT(quit()));

    connect(this, &TestWidget::loggedIn, [=] () {
       qDebug() << "logged in :D";
       getUpdates();
    });
}

void TestWidget::login(QString username, QString password)
{
    m_username = username;

    QNetworkRequest request(BASE_URL + "login");

    QList<QPair<QString, QString>> data;
    data.append(qMakePair(QStringLiteral("password"), password));

    QNetworkReply *reply = sendRequest(request, data);
    QObject::connect(reply, &QNetworkReply::finished, [=]() {
        QByteArray result = reply->readAll();
        QJsonObject object = parseJsonObject(result);
        if (!object.contains("auth_token")) {
            // no auth token in result "{"message":"Dette er ikke riktig passord. Beklager!","status":-100,"logged":false}"
            qDebug() << "no auth token in result" << result;
            emit loginFailed();
            return;
        }
        m_token = object["auth_token"].toString().toLatin1();
        emit loggedIn();

        if (!object.contains("username")) {
            qDebug() << "no username in result";
            emit loginFailed();
            return;
        }
        m_username = object["username"].toString();
    });
}

void TestWidget::logout()
{
    QNetworkRequest request(BASE_URL + "logout");

    QNetworkReply *reply = sendRequest(request);
    QObject::connect(reply, &QNetworkReply::finished, [=]() {
        if (!reply->readAll().isEmpty()) {
            qDebug() << "logout failed";
            emit logoutFailed();
        }
    });
}

void TestWidget::getUpdates(qulonglong timelimit)
{
    QNetworkRequest request(BASE_URL + "updates");

    QList<QPair<QString, QString>> data;
    data.append(qMakePair(QStringLiteral("update_timestamp"), QString::number(timelimit)));

    QNetworkReply *reply = sendRequest(request, data);
    QObject::connect(reply, &QNetworkReply::finished, [=]() {
        QByteArray result = reply->readAll();
        qDebug() << "--- got update ---\n"<< result << "\n---";
        QJsonObject updatesObject = parseJsonObject(result);
        QJsonArray snaps = updatesObject["snaps"].toArray();
        if (snaps.isEmpty()) {
            qWarning() << "no snaps";
            return;
        }

        foreach(QJsonValue snap, snaps) {
            if (!snap.toObject()["c_id"].toString().isEmpty()) {
                //qDebug() << "no id" << snap;
                continue;
            }
            QString mediaId = snap.toObject()["id"].toString();
            getSnap(mediaId);
        }
    });
}

void TestWidget::getStories(qulonglong timelimit)
{
    QNetworkRequest request(BASE_URL + "all_updates");

    QList<QPair<QString, QString>> data;
    data.append(qMakePair(QStringLiteral("update_timestamp"), QString::number(timelimit)));

    QNetworkReply *reply = sendRequest(request, data);
    QObject::connect(reply, &QNetworkReply::finished, [=]() {
        QByteArray result = reply->readAll();
        QJsonObject object = parseJsonObject(result);
        if (object.contains("auth_token")) {
            m_token = object["auth_token"].toString().toLatin1();
        }

        // TODO: parse
        qDebug() << "--- got update ---\n"<< result << "\n---";
    });
}

void TestWidget::getStoryBlob(const QString &id, const QByteArray &key, const QByteArray &iv)
{
    QNetworkRequest request(BASE_URL + "story_blob");

    QList<QPair<QString, QString>> data;
    data.append(qMakePair(QStringLiteral("story_id"), id));


    QNetworkReply *reply = sendRequest(request, data, QNetworkAccessManager::GetOperation);
    QObject::connect(reply, &QNetworkReply::finished, [=]() {
        storeFile(decodeStory(reply->readAll(), key, iv), "story_" + id);

        emit storedStoryBlob(id);
    });
}

void TestWidget::getSnap(const QString &id)
{
    qDebug() << "getting snap" << id;
    QNetworkRequest request(BASE_URL + "blob");

    QList<QPair<QString, QString>> data;
    data.append(qMakePair(QStringLiteral("id"), id));

    QNetworkReply *reply = sendRequest(request, data);
    QObject::connect(reply, &QNetworkReply::finished, [=]() {
        if (reply->error() != QNetworkReply::NoError) {
            qWarning() << "error while getting snap:" << reply->errorString();
            return;
        }

        QByteArray result = reply->readAll();
        QByteArray decoded = decode(result);
        if (isValid(decoded)) {
            storeFile(decoded, "snap_" + id);
        } else if (isValid(result)) {
            storeFile(result, "publicsnap_" + id);
        } else {
            qDebug() << "weird result:" << result.left(10).toHex() << "id:" << id;
        }

        emit storedSnap(id);
    });
}

void TestWidget::markViewed(const QByteArray &id, int duration)
{
    QNetworkRequest request(BASE_URL + "update_snaps");

    QJsonArray events;
    {
        int timestamp = QDateTime::currentMSecsSinceEpoch() / 1000;

        QJsonObject paramObject;
        paramObject.insert("id", QString(id));

        QJsonObject viewedObject;
        viewedObject.insert("eventName", "SNAP_VIEW");
        viewedObject.insert("params", paramObject);
        viewedObject.insert("ts", QString::number(timestamp - duration));
        events.append(viewedObject);

        QJsonObject expiredObject;
        expiredObject.insert("eventName", "SNAP_EXPIRED");
        expiredObject.insert("params", paramObject);
        expiredObject.insert("ts", QString::number(timestamp));
        events.append(expiredObject);
    }

    QList<QPair<QString, QString>> data;
    data.append(qMakePair(QStringLiteral("id"), id));
    data.append(qMakePair(QStringLiteral("events"), QJsonDocument(events).toJson()));

    QNetworkReply *reply = sendRequest(request, data);
    QObject::connect(reply, &QNetworkReply::finished, [=]() {
        QByteArray result = reply->readAll();
        if (result.isEmpty()) {
            emit markedViewed(id);
        } else {
            qWarning() << "failed to mark as viewed: " << result;
        }
    });
}

void TestWidget::setPrivacy(TestWidget::Privacy privacy)
{
    QNetworkRequest request(BASE_URL + "settings");

    QList<QPair<QString, QString>> data;
    data.append(qMakePair(QStringLiteral("action"), QStringLiteral("updatePrivacy")));
    data.append(qMakePair(QStringLiteral("privacySetting"), QString::number(privacy)));

    QNetworkReply *reply = sendRequest(request, data);
    QObject::connect(reply, &QNetworkReply::finished, [=]() {
        QJsonObject object = parseJsonObject(reply->readAll());
        if (!object.contains("param")) {
            qWarning() << "failed to call set privacy";
            return;
        }
        if (object["param"].toInt() != privacy) {
            qWarning() << "failed to change privacy";
        }

        emit privacyChanged(privacy);
    });

}

void TestWidget::changeRelationship(const QString &username, UserAction userAction)
{
    QNetworkRequest request(BASE_URL + "friend");

    QByteArray action;
    switch (userAction) {
    case AddFriend:
        action = "add";
        break;
    case DeleteFriend:
        action = "delete";
        break;
    case BlockUser:
        action = "block";
        break;
    case UnblockUser:
        action = "unblock";
        break;
    default:
        break;
    }

    QList<QPair<QString, QString>> data;
    data.append(qMakePair(QStringLiteral("action"), QStringLiteral("add")));
    data.append(qMakePair(QStringLiteral("friend"), username));

    QNetworkReply *reply = sendRequest(request, data);
    QObject::connect(reply, &QNetworkReply::finished, [=]() {
        // Returns JSON response.
        // Expected messages:
        // Success: '{username} is now your friend!'
        // Pending: '{username} is private. Friend request sent.'
        // Failure: 'Sorry! Couldn't find {username}'

        QJsonObject result = parseJsonObject(reply->readAll());
        if (userAction == AddFriend) {
            qDebug() << result;
            Q_ASSERT(0);
        } else if (userAction == DeleteFriend)  {
            qDebug() << result;
            Q_ASSERT(0);
        } else if (userAction == BlockUser) {
            if (!result.contains("message")) {
                qWarning() << "unable to find message in result";
            }
            if (result["message"].toString() != username + " was blocked") {
                qWarning() << "failed to block user";
            }

            emit userBlocked(username);
        } else if (userAction == UnblockUser) {
            if (!result.contains("message")) {
                qWarning() << "unable to find message in result";
            }
            if (result["message"].toString() != username + " was unblocked") {
                qWarning() << "failed to unblock user";
            }

            emit userUnblocked(username);
        }
    });
}

void TestWidget::sendSnap(const QByteArray &fileData, QList<QString> recipients, int time)
{
    QString mediaId = m_username.toUpper() + "~" + QUuid::createUuid().toString();

    QNetworkRequest request(BASE_URL + "upload");

    QList<QPair<QString, QString>> data;
    data.append(qMakePair(QStringLiteral("media_id"), mediaId));

    QNetworkReply *reply = sendRequest(request, data, QNetworkAccessManager::PostOperation, fileData);
    QObject::connect(reply, &QNetworkReply::finished, [=]() {
        QByteArray response = reply->readAll();
        if (!response.isEmpty()) {
            qDebug() << "unabled to upload: " << response;
            return;
        }
        sendUploadedSnap(mediaId, recipients, time);
    });
}


void TestWidget::sendUploadedSnap(const QString &id, const QList<QString> &recipients, int time)
{
    QNetworkRequest request(BASE_URL + "send");

    QStringList recipientStrings;
    foreach(const QString &recipient, recipients) {
        recipientStrings.append(recipient);
    }


    QList<QPair<QString, QString>> data;
    data.append(qMakePair(QStringLiteral("media_id"), id));
    data.append(qMakePair(QStringLiteral("recipients"), recipientStrings.join(",")));
    data.append(qMakePair(QStringLiteral("time"), QByteArray::number(time)));
    data.append(qMakePair(QStringLiteral("zipped"), QStringLiteral("0")));

    QNetworkReply *reply = sendRequest(request, data);
    QObject::connect(reply, &QNetworkReply::finished, [=]() {
        QByteArray response = reply->readAll();
        if (!response.isEmpty()) {
            qWarning() << "failed to send uploaded snap" << response;
        }

        emit snapSent();
    });
}

QByteArray TestWidget::pad(QByteArray data, int blocksize)
{
    int padCount = blocksize - (data.length() % blocksize);
    for (int i=0; i < padCount; i++) {
        data += char(padCount);
    }
    return data;
}

QByteArray TestWidget::decode(QByteArray inputData)
{
    if (inputData.length() % 16) {
        qWarning() << "encoded data not a multiple of 16";
        return inputData;
    }

    // ECB, by hand, because everything else sucks
    QByteArray ret(inputData.length(), '\0');
    aes_context ctx;
    aes_set_key(&ctx, (uint8*)BLOBKEY, 128);
    char *decodedBuffer = ret.data();
    for (int i=0; i<inputData.length(); i+=16) {
        aes_decrypt(&ctx, (unsigned char*)inputData.constData() + i, (unsigned char*)decodedBuffer + i);
    }

    return ret;
}

QByteArray TestWidget::decodeStory(QByteArray input, QByteArray key, QByteArray iv)
{
    if (input.length() % 16) {
        qWarning() << "encoded data not a multiple of 16";
        return input;
    }

    // CBC, by hand, because everything else sucks
    QByteArray ret;
    aes_context ctx;
    aes_set_key(&ctx, (uint8*)BLOBKEY, 128);
    char *inputBuffer = input.data();
    char *ivBuffer = iv.data();
    for (int i=0; i<input.length() / 16; i++) {
        for (int j=0; j<16; j++) {
            inputBuffer[i + j] ^= ivBuffer[j];
        }
        aes_decrypt(&ctx, (unsigned char*)input.data() + (i * 16), (unsigned char*)ivBuffer);
        ret += QByteArray::fromRawData(ivBuffer, 16);
    }

    return ret;
}

QByteArray TestWidget::encode(QByteArray inputData)
{
    inputData = pad(inputData);

    // ECB, by hand, because everything else sucks
    QByteArray ret(inputData.length(), '\0');
    char *encodedBuffer = ret.data();
    aes_context ctx;
    aes_set_key(&ctx, (uint8*)BLOBKEY, 128);
    for (int i=0; i<inputData.length(); i+=16) {
        aes_encrypt(&ctx, (unsigned char*)inputData.constData() + i, (unsigned char*)encodedBuffer + i);
    }

    return ret;
}

QByteArray TestWidget::extension(TestWidget::MediaType type)
{
    switch(type) {
    case Video:
    case VideoNoAudio:
        return "mp4";
    case Image:
        return "jpg";
    }
    return "";
}

QNetworkReply *TestWidget::sendRequest(QNetworkRequest request, QList<QPair<QString, QString> > data, QNetworkAccessManager::Operation operation, const QByteArray &fileData)
{
    QByteArray timestamp = QByteArray::number(QDateTime::currentMSecsSinceEpoch());

    request.setHeader(QNetworkRequest::UserAgentHeader, "Snapchat/6.1.2 (iPhone6,2; iOS 7.0.4; gzip)");

    QByteArray token = requestToken(m_token, timestamp);

    data.append(qMakePair(QStringLiteral("timestamp"), timestamp));
    data.append(qMakePair(QStringLiteral("req_token"), token));
    data.append(qMakePair(QStringLiteral("username"), m_username));

    QNetworkReply *reply = 0;
    if (operation == QNetworkAccessManager::PostOperation) {
        if (fileData.isEmpty()) {
            QByteArray body;
            foreach (auto item, data) {
                body.append(item.first.toUtf8().toPercentEncoding());
                body.append("=");
                body.append(item.second.toUtf8().toPercentEncoding());
                body.append("&");
            }
            if (data.length() > 0) {
                body.chop(1); // strip last &
            }

            request.setHeader(QNetworkRequest::ContentTypeHeader,"application/x-www-form-urlencoded");
            reply = m_accessManager.post(request, body);
        } else {
            QString mimetype;
            int mediaType;
            if (isImage(fileData)) {
                mediaType = Image;
                mimetype = "image/jpeg";
            } else if (isVideo(fileData)) {
                mediaType = Video;
                mimetype = "video/mp4";
            } else {
                qWarning() << "trying to send invalid data";
                emit sendFailed();
                return 0;
            }
            data.append(qMakePair(QStringLiteral("type"), QString::number(mediaType)));

            QHttpMultiPart *multiPart = new QHttpMultiPart;
            QHttpPart filepart;
            filepart.setHeader(QNetworkRequest::ContentTypeHeader, QVariant(mimetype));
            filepart.setHeader(QNetworkRequest::ContentDispositionHeader, QVariant("form-data; name=\"data\""));
            filepart.setBody(encode(fileData));
            multiPart->append(filepart);

            foreach (auto item, data) {
                multiPart->append(createPart(item.first, item.second));
            }

            reply = m_accessManager.post(request, multiPart);
            multiPart->setParent(reply);
        }

    } else if (operation == QNetworkAccessManager::GetOperation) {
        QUrl url = request.url();

        QUrlQuery query;
        query.addQueryItem("timestamp", timestamp);
        query.addQueryItem("req_token", token);

        foreach(auto item, data) {
            query.addQueryItem(item.first, item.second);
        }

        url.setQuery(query);

        request.setUrl(url);

        reply = m_accessManager.get(request);
    } else {
        Q_ASSERT(0);
    }

    return reply;
}

QHttpPart TestWidget::createPart(const QString &key, const QString &value)
{
    QHttpPart part;
    part.setHeader(QNetworkRequest::ContentTypeHeader, QVariant("text/plain"));
    part.setHeader(QNetworkRequest::ContentDispositionHeader, QVariant("form-data; name=\"" + key + "\""));
    part.setBody(value.toUtf8());
    return part;
}

QJsonObject TestWidget::parseJsonObject(const QByteArray &data)
{
    QJsonDocument result = QJsonDocument::fromJson(data);
    if (result.isNull()) {
        qDebug() << "unable to parse result: " << data;
        return QJsonObject();
    }

    QJsonObject object = result.object();
    if (object.isEmpty()) {
        qDebug() << "no json object in result";
    }
    return object;
}

void TestWidget::storeFile(const QByteArray &data, const QString &filename)
{
    qDebug() << "writing" << data.length() / (1024.0 * 1024.0) << "MB to" << filename;
    Q_ASSERT(data.length() < 1024 * 1024 * 10);

    QString extension;
    if (isVideo(data)) {
        extension = ".mp4";
    } else if (isImage(data)) {
        extension = ".jpg";
    } else if (isZip(data)) {
        extension = ".zip";
    } else {
        extension = ".bin";
    }

    QFile file(m_outputPath + filename + extension);
    if (!file.open(QIODevice::WriteOnly)) {
        qWarning() << "unable to write file" << m_outputPath << filename;
        emit writeFileFailed();
        return;
    }

    file.write(data);
    file.close();
}

QByteArray TestWidget::requestToken(QByteArray secA, QByteArray secB)
{
    QByteArray hashA = QCryptographicHash::hash(SECRET + secA, QCryptographicHash::Sha256).toHex();
    QByteArray hashB = QCryptographicHash::hash(secB + SECRET, QCryptographicHash::Sha256).toHex();

    static int mergePattern[] = {0, 0, 0, 1,
                                 1, 1, 0, 1,
                                 1, 1, 1, 0,
                                 1, 1, 1, 0,
                                 0, 0, 1, 1,
                                 1, 1, 0, 1,
                                 0, 1, 0, 1,
                                 1, 1, 1, 0,
                                 1, 1, 0, 1,
                                 0, 0, 0, 1,
                                 0, 0, 1, 1,
                                 1, 0, 0, 1,
                                 1, 0, 0, 0,
                                 1, 1, 0, 0,
                                 0, 1, 0, 0,
                                 0, 1, 1, 0 };

    QByteArray res;
    for (int i=0; i<64; i++) {
        if (mergePattern[i]) {
            res += hashB[i];
        } else {
            res += hashA[i];
        }
    }

    return res;
}

