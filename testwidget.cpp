#include "testwidget.h"
#include <QCryptographicHash>
#include <QtCrypto>
#include <QHttpMultiPart>
#include <QUrlQuery>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QFile>
#include <QUuid>

#define SECRET "iEk21fuwZApXlz93750dmW22pw389dPwOk"

#define BLOBKEY "M02cnQ51Ji97vwT4"

#define BASE_URL QStringLiteral("https://feelinsonice-hrd.appspot.com/bq/")

TestWidget::TestWidget(QWidget *parent) :
    QWidget(parent),
    m_token("m198sOkJEn37DjqZ32lpRu76xmw288xSQ9"),
    m_outputPath("/home/sandsmark/tmp/")
{
}

void TestWidget::login(QString username, QString password)
{
    m_username = username;

    QNetworkRequest request(BASE_URL + "login");

    QHttpMultiPart *data = new QHttpMultiPart;
    data->append(createPart("password", password));

    QNetworkReply *reply = sendRequest(request, data);
    QObject::connect(reply, &QNetworkReply::finished, [=]() {
        QJsonObject object = parseJsonObject(reply->readAll());
        if (!object.contains("auth_token")) {
            qDebug() << "no auth token in result";
            emit loginFailed();
            return;
        }
        m_token = object["auth_token"].toString().toLatin1();

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

    QHttpMultiPart *data = new QHttpMultiPart;
    data->append(createPart("update_timestamp", QByteArray::number(timelimit)));

    QNetworkReply *reply = sendRequest(request, data);
    QObject::connect(reply, &QNetworkReply::finished, [=]() {
        qDebug() << "--- got update ---\n"<< reply->readAll() << "\n---";
    });
}

void TestWidget::getStories(qulonglong timelimit)
{
    QNetworkRequest request(BASE_URL + "all_updates");

    QHttpMultiPart *data = new QHttpMultiPart;
    data->append(createPart("update_timestamp", QByteArray::number(timelimit)));

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

void TestWidget::getStoryBlob(const QByteArray &id, const QByteArray &key, const QByteArray &iv)
{
    QNetworkRequest request(BASE_URL + "story_blob");

    QHttpMultiPart *data = new QHttpMultiPart;
    data->append(createPart("story_id", id));

    QNetworkReply *reply = sendRequest(request, data, QNetworkAccessManager::GetOperation);
    QObject::connect(reply, &QNetworkReply::finished, [=]() {
        storeFile(decodeStory(reply->readAll(), key, iv), "story_" + id);

        emit storedStoryBlob(id);
    });
}

void TestWidget::getSnap(const QByteArray &id)
{
    QNetworkRequest request(BASE_URL + "blob");

    QHttpMultiPart *data = new QHttpMultiPart;
    data->append(createPart("id", id));

    QNetworkReply *reply = sendRequest(request, data);
    QObject::connect(reply, &QNetworkReply::finished, [=]() {
        storeFile(decode(reply->readAll()), "snap_" + id);

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

    QHttpMultiPart *data = new QHttpMultiPart;
    data->append(createPart("id", id));
    data->append(createPart("events", QJsonDocument(events).toJson()));

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

    QHttpMultiPart *data = new QHttpMultiPart;
    data->append(createPart("action", "updatePrivacy"));
    data->append(createPart("privacySetting", QByteArray::number(privacy)));

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

void TestWidget::changeRelationship(const QByteArray &username, UserAction userAction)
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

    QHttpMultiPart *data = new QHttpMultiPart;
    data->append(createPart("action", "add"));
    data->append(createPart("friend", username));

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

void TestWidget::sendSnap(const QByteArray &fileData, QList<QByteArray> recipients, int time)
{
    int mediaType;
    QString mimetype;

    if (isImage(fileData)) {
        mediaType = Image;
        mimetype = "image/jpeg";
    } else if (isVideo(fileData)) {
        mediaType = Video;
        mimetype = "video/mp4";
    } else {
        qWarning() << "trying to send invalid data";
        emit sendFailed();
        return;
    }

    QByteArray mediaId = m_username.toUpper().toUtf8() + "~" + QUuid::createUuid().toString().toLatin1();

    QNetworkRequest request(BASE_URL + "upload");

    QHttpMultiPart *data = new QHttpMultiPart;
    data->append(createPart("type", QByteArray::number(mediaType)));
    data->append(createPart("media_id", mediaId));

    QHttpPart filepart;
    filepart.setHeader(QNetworkRequest::ContentTypeHeader, QVariant(mimetype));
    filepart.setHeader(QNetworkRequest::ContentDispositionHeader, QVariant("form-data; name=\"data\""));
    filepart.setBody(encode(fileData));
    data->append(filepart);

    QNetworkReply *reply = sendRequest(request, data);
    QObject::connect(reply, &QNetworkReply::finished, [=]() {
        QByteArray response = reply->readAll();
        if (!response.isEmpty()) {
            qDebug() << "unabled to upload: " << response;
            return;
        }
        sendUploadedSnap(mediaId, recipients, time);
    });
}

QByteArray TestWidget::pad(QByteArray data, int blocksize)
{
    int padCount = blocksize - data.length() % blocksize;
    for (int i=0; i < padCount; i++) {
        data += padCount;
    }
    return data;
}

QByteArray TestWidget::decode(QByteArray data)
{
//    cipher = AES.new(BLOB_ENCRYPTION_KEY, AES.MODE_ECB)
//    return cipher.decrypt(pkcs5_pad(data))
    QCA::Cipher cipher(QStringLiteral("aes128"),
                       QCA::Cipher::ECB,
                       QCA::Cipher::DefaultPadding,
                       QCA::Decode,
                       QCA::SymmetricKey(QByteArray(BLOBKEY)));
    return cipher.process(data).toByteArray();
}

QByteArray TestWidget::decodeStory(QByteArray data, QByteArray key, QByteArray iv)
{
    //cipher = AES.new(key, AES.MODE_CBC, iv)
    //return cipher.decrypt(pkcs5_pad(data))
    QCA::Cipher cipher(QStringLiteral("aes128"),
                       QCA::Cipher::CBC,
                       QCA::Cipher::DefaultPadding,
                       QCA::Decode,
                       QCA::SymmetricKey(key),
                       QCA::InitializationVector(iv));
    return cipher.process(data).toByteArray();
}

QByteArray TestWidget::encode(QByteArray data)
{
    //cipher = AES.new(BLOB_ENCRYPTION_KEY, AES.MODE_ECB)
    //return cipher.encrypt(pkcs5_pad(data))

    QCA::Cipher cipher(QStringLiteral("aes128"),
                       QCA::Cipher::ECB,
                       QCA::Cipher::DefaultPadding,
                       QCA::Encode,
                       QCA::SymmetricKey(QByteArray(BLOBKEY)));
    return cipher.process(data).toByteArray();
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

QNetworkReply *TestWidget::sendRequest(QNetworkRequest request, QHttpMultiPart *data, QNetworkAccessManager::Operation operation)
{
    QByteArray timestamp = QByteArray::number(QDateTime::currentMSecsSinceEpoch());

    request.setHeader(QNetworkRequest::UserAgentHeader, "Snapchat/6.1.2 (iPhone6,2; iOS 7.0.4; gzip)");

    QByteArray token = requestToken(m_token, timestamp);

    QNetworkReply *reply = 0;
    if (operation == QNetworkAccessManager::PostOperation) {
        if (!data) {
            data = new QHttpMultiPart;
            data->setContentType(QHttpMultiPart::FormDataType);
        }

        data->append(createPart("timestamp", timestamp));
        data->append(createPart("req_token", token));
        data->append(createPart("username", m_username));

        reply = m_accessManager.post(request, data);
    } else if (operation == QNetworkAccessManager::GetOperation) {
        QUrl url = request.url();

        QUrlQuery query;
        query.addQueryItem("timestamp", timestamp);
        query.addQueryItem("req_token", token);
        url.setQuery(query);

        request.setUrl(url);

        reply = m_accessManager.get(request);
    } else {
        Q_ASSERT(0);
    }

    data->setParent(reply);
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
        qDebug() << "unable to parse result";
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
}

void TestWidget::sendUploadedSnap(const QByteArray &id, const QList<QByteArray> &recipients, int time)
{
    QNetworkRequest request(BASE_URL + "send");

    QStringList recipientStrings;
    foreach(const QByteArray &recipient, recipients) {
        recipientStrings.append(QString::fromUtf8(recipient));
    }

    QHttpMultiPart *data = new QHttpMultiPart;
    data->append(createPart("media_id", id));
    data->append(createPart("recipients", recipientStrings.join(",")));
    data->append(createPart("time", QByteArray::number(time)));
    data->append(createPart("zipped", "0"));

    QNetworkReply *reply = sendRequest(request, data);
    QObject::connect(reply, &QNetworkReply::finished, [=]() {
        QByteArray response = reply->readAll();
        if (!response.isEmpty()) {
            qWarning() << "failed to send uploaded snap" << response;
        }

        emit snapSent();
    });
}

QByteArray TestWidget::requestToken(QByteArray secA, QByteArray secB)
{
    QByteArray hashA = QCryptographicHash::hash(SECRET + secA, QCryptographicHash::Sha3_256).toHex();
    QByteArray hashB = QCryptographicHash::hash(secB + SECRET, QCryptographicHash::Sha3_256).toHex();

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
    for (int i=0; i<16; i++) {
        if (mergePattern[i]) {
            res += hashB[i];
        } else {
            res += hashA[i];
        }
    }

    return res;
}

