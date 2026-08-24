#include "ContactBook.h"

#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>



ContactBook::ContactBook()
	: QObject(nullptr)
	, m_compDepID(-1)
{}

ContactBook::~ContactBook()
{}

ContactBook& ContactBook::getInstance() {
	static ContactBook instance;
	return instance;
}

void ContactBook::loadFromJson(const QByteArray& json) {
	//解析 JSON 快照信息

	//解析失败降级：缓存保持原状，登录流程继续（不崩溃）
	QJsonParseError err;
	QJsonDocument doc = QJsonDocument::fromJson(json, &err);
	if (err.error != QJsonParseError::NoError) {
		qDebug() << QStringLiteral("[ContactBook] 快照解析失败：") << err.errorString();
		return;
	}

	//先清空再填充：静默重登时全量刷新的关键
	this->m_employees.clear();
	this->m_departments.clear();

	QJsonObject root = doc.object();
	QJsonArray employees = root.value("tab_employees").toArray();
	QJsonArray department = root.value("tab_department").toArray();

	//解析 tab_employees 全量表
	for (const QJsonValue& v : employees) {
		QJsonObject emp = v.toObject();
		EmployeeInfo empInfo;

		empInfo.name = emp.value("employee_name").toString();
		empInfo.sign = emp.value("employee_sign").toString();
		empInfo.picture = emp.value("picture").toString();
		empInfo.depID = emp.value("departmentID").toInt();

		this->m_employees.insert(emp.value("employeeID").toInt(), empInfo);
	}
	
	//解析 tab_department 全量表

	for (const QJsonValue& v : department) {
		QJsonObject dep = v.toObject();
		DepartmentInfo depInfo;

		depInfo.name = dep.value("department_name").toString();
		depInfo.sign = dep.value("sign").toString();
		depInfo.picture = dep.value("picture").toString();
	
		this->m_departments.insert(dep.value("departmentID").toInt(), depInfo);
	}


	//预解析公司群ID
	this->m_compDepID = -1;
	for (auto it = this->m_departments.cbegin(); it != this->m_departments.cend(); ++it) {
		if (it.value().name == QStringLiteral("公司群")) {
			this->m_compDepID = it.key();
			break;
		}
	}

	qDebug() << QStringLiteral("[ContactBook] 快照已加载：%1名员工 / %2个部门").arg(this->m_employees.size()).arg(this->m_departments.size());
}

bool ContactBook::isGroup(int uid) const {
	//uid 命中部门表 → 是群
	return this->m_departments.contains(uid);
}

int ContactBook::compDepID() const {
	//loadFromJson 时已预解析，直接返回
	return this->m_compDepID;
}

int ContactBook::selfDepID(int empID) const {
	//QMap::value 查无此 key 返回默认构造值（depID = -1）
	return this->m_employees.value(empID).depID;
}

QList<int> ContactBook::groupMembers(int depID) const {
	//公司群 → 全部员工；部门群 → 该部门员工
	//QMap 按 key 升序，与原 SQL 查询的员工ID顺序一致
	if (depID == this->m_compDepID) {
		return this->m_employees.keys();
	}

	QList<int> members;
	for (auto it = this->m_employees.begin(); it != this->m_employees.end(); it++) {
		if (it.value().depID == depID) {
			members.append(it.key());
		}
	}
	return members;
}

EmployeeInfo ContactBook::employeeInfo(int empID) const {
	//查无此ID返回默认空值（调用方按空串处理）
	return this->m_employees.value(empID);
}

DepartmentInfo ContactBook::departmentInfo(int depID) const {
	//查无此ID返回默认空值（调用方按空串处理）
	return this->m_departments.value(depID);
}


