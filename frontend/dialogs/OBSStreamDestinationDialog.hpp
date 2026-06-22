#pragma once

#include <oauth/Auth.hpp>

#include <obs.hpp>

#include <QLabel>
#include <QDialog>
#include <QString>

#include <memory>

class QCheckBox;
class QComboBox;
class QLineEdit;
class QPushButton;
class QStackedWidget;

struct OBSStreamDestinationResult {
	std::string displayName;
	std::string type;
	OBSData settings;
};

class OBSStreamDestinationDialog : public QDialog {
	Q_OBJECT

	QComboBox *serviceCombo = nullptr;
	QStackedWidget *authStack = nullptr;
	QComboBox *serverCombo = nullptr;
	QStackedWidget *serverStack = nullptr;
	QLineEdit *customServerEdit = nullptr;
	QLineEdit *keyEdit = nullptr;
	QLabel *keyLabel = nullptr;
	QLineEdit *nameEdit = nullptr;
	QCheckBox *useAuthCheck = nullptr;
	QLineEdit *authUsernameEdit = nullptr;
	QLineEdit *authPwEdit = nullptr;
	QPushButton *connectAccountButton = nullptr;
	QPushButton *useStreamKeyButton = nullptr;

	std::shared_ptr<Auth> auth;

	QString lastService;

	enum class ListOpt : int { ShowAll = 1, Custom, WHIP };
	enum class AuthPage : int { Connect = 0, Destination = 1 };

	bool IsCustomService() const;
	bool IsWHIP() const;
	bool IsAuthService() const;

	void LoadServices(bool showAll);
	void UpdateServerList();
	void UpdateFieldVisibility();
	void OnAuthConnected();
	bool Validate() const;
	std::string DefaultDisplayName() const;

public:
	explicit OBSStreamDestinationDialog(QWidget *parent = nullptr);

	static bool GetDestination(QWidget *parent, OBSStreamDestinationResult &result);
};
