#pragma once

#include <QObject>
#include <QMap>
#include <QList>


// employees 快照（tab_employees 一行的副本）
struct EmployeeInfo {
	QString name;			//employee_name
	QString sign;			//employee_sign
	QString picture;		//picture（路径，与原数据库值一致）
	int depID = -1;			//departmentID
};

// department 快照（tab_department 一行的副本）
struct DepartmentInfo {
	QString name;			//department_name
	QString sign;			//sign
	QString picture;		//picture
};


//通讯录缓存（内存快照，随登录/重登全量刷新）
//数据源：LoginResponse 尾部 JSON
class ContactBook  : public QObject
{
	Q_OBJECT

public:
	static ContactBook& getInstance();

	void loadFromJson(const QByteArray& json);

	bool isGroup(int uid) const;					//uid 是否为群（m_departments 是否含此 key）
	int compDepID() const;							//公司群 departmentID（查无返回 -1）
	int selfDepID(int empID) const;					//某员工所属部门ID（查无返回 -1）
	QList<int> groupMembers(int depID) const;		//群成员列表：公司群 → 全部员工；部门群 → 按部门过滤
	EmployeeInfo employeeInfo(int empID) const;		//员工信息（查无返回默认空值）
	DepartmentInfo departmentInfo(int depID) const;	//部门信息（查无返回默认空值）

private:
	ContactBook();
	~ContactBook();
	ContactBook(const ContactBook&) = delete;
	ContactBook& operator=(const ContactBook&) = delete;

private:
	//成员变量
	QMap<int, EmployeeInfo> m_employees;		// empID - empInfo 的映射
	QMap<int, DepartmentInfo> m_departments;	// depID - depInfo 的映射
	
	int m_compDepID;		//公司群 depID
};

