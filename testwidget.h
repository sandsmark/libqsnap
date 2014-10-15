#ifndef TESTWIDGET_H
#define TESTWIDGET_H

#include <QWidget>
#include <QNetworkReply>
#include <QNetworkAccessManager>
#include <QHttpPart>
#include <QJsonObject>

class QHttpMultiPart;

class TestWidget : public QWidget
{
    Q_OBJECT

    enum MediaType {
        Image = 0,
        Video = 1,
        VideoNoAudio = 2
    };
    enum FriendStatus {
        Confirmed = 0,
        Unconfirmed = 1,
        Blocked = 2
    };
    enum Privacy {
        PrivacyEveryone = 0,
        PrivacyFriendsOnly = 1
    };
    enum UserAction {
        AddFriend,
        DeleteFriend,
        BlockUser,
        UnblockUser
    };

public:
    explicit TestWidget(QWidget *parent = 0);

    void login(QString username, QString password);
    void logout();
    void getUpdates(qulonglong timelimit = 0);
    void getStories(qulonglong timelimit = 0);
    void getStoryBlob(const QByteArray &id, const QByteArray &key, const QByteArray &iv);
    void getSnap(const QByteArray &id);
    void markViewed(const QByteArray &id, int duration = 1);
    void setPrivacy(Privacy privacy);
    void changeRelationship(const QByteArray &username, UserAction userAction);
    void sendSnap(const QByteArray &fileData, QList<QByteArray> recipients, int time = 5);

signals:
    void loginFailed();
    void logoutFailed();

    void writeFileFailed();
    void storedStoryBlob(QByteArray id);
    void storedSnap(QByteArray id);
    void markedViewed(QByteArray id);

    void privacyChanged(Privacy privacy);

    void friendAdded(QByteArray username);
    void userNotFound(QByteArray username);
    void friendRequestSent(QByteArray username);
    void friendDeleted(QByteArray username);
    void userBlocked(QByteArray username);
    void userUnblocked(QByteArray username);

    void sendFailed();
    void snapSent();

private:
    QByteArray pad(QByteArray data, int blocksize = 16);

    QByteArray requestToken(QByteArray secA, QByteArray secB);

    QByteArray decode(QByteArray data);
    QByteArray decodeStory(QByteArray data, QByteArray key, QByteArray iv);

    QByteArray encode(QByteArray data);

    static inline bool isVideo(const QByteArray &data) { return (data.length() > 2 && data[0] == 0 && data[1] == 0); }
    static inline bool isImage(const QByteArray &data) { return (data.length() > 2 && data[0] == '\xFF' && data[1] == '\xD8'); }
    static inline bool isZip(const QByteArray &data) { return (data.length() > 2 && data[0] == 'P' && data[1] == 'K'); }

    QByteArray extension(MediaType type);

    QNetworkReply *sendRequest(QNetworkRequest sendRequest,
                           QHttpMultiPart *data = 0,
                           QNetworkAccessManager::Operation operation = QNetworkAccessManager::PostOperation);

    QHttpPart createPart(const QString &key, const QString &value);
    QJsonObject parseJsonObject(const QByteArray &data);
    void storeFile(const QByteArray &data, const QString &filename);
    void sendUploadedSnap(const QByteArray &id, const QList<QByteArray> &recipients, int time);

    QByteArray m_token;
    QNetworkAccessManager m_accessManager;
    QString m_username;

    QString m_outputPath;
};

#endif // TESTWIDGET_H
