#include "CCMainWindow.h"
#include "SkinWindow.h"
#include "SysTray.h"
#include "NotifyManager.h"
#include "RootContactItem.h"
#include "ContactItem.h"
#include "WindowManager.h"
#include "TcpClient.h"
#include "UserLogin.h"

#include <QProxyStyle>
#include <QPainter>
#include <QTimer>
#include <QMouseEvent>
#include <QApplication>
#include <QSqlQuery>
#include <QSqlError>
#include <QMessageBox>



//改变默认的部件风格
class CustomProxyStyle :public QProxyStyle
{
public:
    virtual void drawPrimitive(PrimitiveElement element, const QStyleOption* option,
        QPainter* painter, const QWidget* widget = nullptr) const
    {
        if (element == PE_FrameFocusRect)
        {
            // 去掉windows中部件默认的边框或虚线框，部件获取焦点时直接放回，不进行绘制
            return;
        }
        else
        {
            QProxyStyle::drawPrimitive(element, option, painter, widget);
        }
    }
};



CCMainWindow::CCMainWindow(int empID, QWidget* parent)
    : BasicWindow(parent) 
    , m_empID(empID)
{
	ui.setupUi(this);
    this->setAttribute(Qt::WA_DeleteOnClose);
	this->setWindowFlags(windowFlags() | Qt::Tool);
	this->loadStyleSheet("CCMainWindow");
    this->initControl();
    this->initTimer();
    this->initUserInfo();

    WindowManager::getInstance().m_empID = this->m_empID;
}

CCMainWindow::~CCMainWindow()
{}

void CCMainWindow::initControl() {

	ui.treeWidget->setStyle(new CustomProxyStyle);

    //连接信号槽
    connect(ui.sysmin, &QPushButton::clicked, this, &BasicWindow::onShowHide);
    connect(ui.sysclose, &QPushButton::clicked, this, &BasicWindow::onShowClose);
    connect(&NotifyManager::getInstance(), &NotifyManager::signalSkinChanged, this, [this]() {
        this->updateSearchStyle();
        });
    connect(&TcpClient::getInstance(), &TcpClient::signalKickedOut,
        this, &CCMainWindow::onKickedOut);

    //添加应用控件
    QHBoxLayout* appupLayout = new QHBoxLayout;     //创建水平布局
    appupLayout->setContentsMargins(0, 0, 0, 0);    //设置布局边距
    appupLayout->addWidget(this->addOtherAppExtension(":/Resources/MainWindow/app/app_7.png", "app_7"));    //将应用按钮加入布局
    appupLayout->addWidget(this->addOtherAppExtension(":/Resources/MainWindow/app/app_2.png", "app_2"));
    appupLayout->addWidget(this->addOtherAppExtension(":/Resources/MainWindow/app/app_3.png", "app_3"));
    appupLayout->addWidget(this->addOtherAppExtension(":/Resources/MainWindow/app/app_4.png", "app_4"));
    appupLayout->addWidget(this->addOtherAppExtension(":/Resources/MainWindow/app/app_5.png", "app_5"));
    appupLayout->addWidget(this->addOtherAppExtension(":/Resources/MainWindow/app/app_6.png", "app_6"));
    appupLayout->addStretch();
    appupLayout->addWidget(this->addOtherAppExtension(":/Resources/MainWindow/app/skin.png", "app_skin"));
    appupLayout->setSpacing(1);
    ui.appWidget->setLayout(appupLayout);           //将布局加入控件

    ui.bottomLayout_up->addWidget(this->addOtherAppExtension(":/Resources/MainWindow/app/app_10.png", "app_10"));
    ui.bottomLayout_up->addWidget(this->addOtherAppExtension(":/Resources/MainWindow/app/app_8.png", "app_8"));
    ui.bottomLayout_up->addWidget(this->addOtherAppExtension(":/Resources/MainWindow/app/app_11.png", "app_11"));
    ui.bottomLayout_up->addWidget(this->addOtherAppExtension(":/Resources/MainWindow/app/app_9.png", "app_9"));
    ui.bottomLayout_up->addStretch();

    //初始化联系人树
    this->initContactTree();

    //安装事件过滤器
    ui.lineEdit->installEventFilter(this);
    ui.searchLineEdit->installEventFilter(this);
    
    //创建托盘对象
    SysTray* systray = new SysTray(this);
}

void CCMainWindow::initTimer() {
    QTimer* timer = new QTimer(this);
    timer->setInterval(500);
    connect(timer, &QTimer::timeout, this, [this]() {
        static int level = 0;
        if (level == 99) {
            level = 0;
        }
        level++;
        this->setLevelPixmap(level); 
    });
    timer->start();
}

void CCMainWindow::initUserInfo() {
    //查询数据库，根据用户 employeeID 获取用户信息
    QSqlQuery query;
    query.prepare("SELECT `employee_name`, `employee_sign`, `picture` FROM `tab_employees` WHERE `employeeID` = ?");
    query.addBindValue(this->m_empID);
    query.exec();
    query.next();

    QString username = query.value(0).toString();
    this->setProperty("username", username);		//存入动态属性，供 resizeEvent 取用，避免硬编码
    this->setUserName(username);
    this->setHeadPixmap(query.value(2).toString());
    this->setLevelPixmap(12);
    this->setStatusMenuIcon(":/Resources/MainWindow/StatusSucceeded.png");
}

void CCMainWindow::updateSearchStyle() {
    //还原搜索栏样式
    ui.searchWidget->setStyleSheet(QString("QWidget#searchWidget{background-color:rgba(%1,%2,%3,50);border-bottom:1px solid rgba(%1,%2,%3,30)}\
																 QPushButton#searchBtn{border-image:url(:/Resources/MainWindow/search/search_icon.png)}")
                                           .arg(m_colorBackground.red())
                                           .arg(m_colorBackground.green())
                                           .arg(m_colorBackground.blue()));
}

void CCMainWindow::addGroupItem(QTreeWidgetItem* pRootGroupItem, int depID) {
    //创建子项 QTreeWidgetItem
    QTreeWidgetItem* pChild = new QTreeWidgetItem();

    //为 QTreeWidgetItem 节点绑定一份 “隐形的自定义业务数据”
    pChild->setData(0, Qt::UserRole, 1);        //子项 key <0, Qt::UserRole>,  value = 1
    pChild->setData(0, Qt::UserRole + 1, depID);     //群聊 key <0, Qt::UserRole + 1>, value = depID


    //查询数据库，根据 departmentID 获取对应的 picture , name
    QSqlQuery query;
    query.prepare("SELECT `picture`, `department_name` FROM `tab_department` WHERE `departmentID` = ?");
    query.addBindValue(depID);
    query.exec();
    query.next();
    QPixmap groupPix;
    groupPix.load(query.value(0).toString());
    QString groupName = query.value(1).toString();
    
    //创建子项贴纸，设置贴纸信息
    ContactItem* pContactItem = new ContactItem(ui.treeWidget);
    QPixmap pix;
    pix.load(":/Resources/MainWindow/head_mask.png");
    pContactItem->setHeadPixmap(this->getRoundImage(groupPix, pix, pContactItem->getHeadLabelSize()));
    pContactItem->setUserName(groupName);

    //将子项添加到根项上
    pRootGroupItem->addChild(pChild);
    ui.treeWidget->setItemWidget(pChild, 0, pContactItem);
}

void CCMainWindow::doLocalLogout() {
    //退出登录本地清理

    //1. 关闭聊天窗口壳（内部所有聊天窗口随之销毁，WindowManager::destroyed 置空指针）
    if (WindowManager::getInstance().getTalkWindowShell()) {
        WindowManager::getInstance().getTalkWindowShell()->close();
    }

    //2. 关闭主窗
    this->close();

    //3. 重建登录窗
    UserLogin* userLogin = new UserLogin;
    userLogin->show();
}

void CCMainWindow::setUserName(const QString& username) {
    //ui.nameLabel->adjustSize();
    //根据当前标签的宽度来省略文本，Qt::ElideRight 表示在右侧添加省略号（…）
    QString name = ui.nameLabel->fontMetrics().elidedText(username, Qt::ElideRight, ui.nameLabel->width());
    ui.nameLabel->setText(name);
}

void CCMainWindow::setLevelPixmap(int level) {
    QPixmap levelPixmap(ui.levelBtn->size());
    levelPixmap.fill(Qt::transparent);
    QPainter painter(&levelPixmap);
    painter.drawPixmap(0, 4, QPixmap(":/Resources/MainWindow/lv.png"));

    int unitNum = level % 10;   //个位数
    int tenNum = level / 10;    //十位数

    //十位，截取图片中的部分进行绘制
    //drawPixmap(绘制点x，绘制点y, 图片，图片左上角x, 图片左上角y, 拷贝的宽度，拷贝的高度)
    painter.drawPixmap(10, 4, QPixmap(":/Resources/MainWindow/levelvalue.png"), tenNum * 6, 0, 6, 7);

    //个位
    painter.drawPixmap(16, 4, QPixmap(":/Resources/MainWindow/levelvalue.png"), unitNum * 6, 0, 6, 7);

    ui.levelBtn->setIcon(levelPixmap);
    ui.levelBtn->setIconSize(ui.levelBtn->size());
}

void CCMainWindow::setHeadPixmap(const QString& headPath) {
    QPixmap pix;
    pix.load(":/Resources/MainWindow/head_mask.png");       //加载遮罩

    ui.headLabel->setPixmap(this->getRoundImage(QPixmap(headPath), pix, ui.headLabel->size()));
}

void CCMainWindow::setStatusMenuIcon(const QString& statusPath) {
    QPixmap statusBtnPixmap(ui.stausBtn->size());
    statusBtnPixmap.fill(Qt::transparent);
    
    QPainter painter(&statusBtnPixmap);
    painter.drawPixmap(4, 4, QPixmap(statusPath));
    
    ui.stausBtn->setIcon(statusBtnPixmap);
    ui.stausBtn->setIconSize(ui.stausBtn->size());
}

QWidget* CCMainWindow::addOtherAppExtension(const QString& appPath, const QString& appName) {
    QPushButton* btn = new QPushButton(this);
    btn->setFixedSize(20, 20);
     
    QPixmap pixmap(btn->size());
    pixmap.fill(Qt::transparent);

    QPainter painter(&pixmap);
    QPixmap appPixmap(appPath);
    painter.drawPixmap((btn->width() - appPixmap.width()) / 2, (btn->height() - appPixmap.height()) / 2, appPixmap);

    btn->setIcon(pixmap);
    btn->setIconSize(btn->size());
    btn->setObjectName(appName);
    btn->setProperty("hasborder", true);

    connect(btn, &QPushButton::clicked, this, &CCMainWindow::onBottonAppClicked);

    return btn;
}

void CCMainWindow::initContactTree() {
    //连接信号槽
    connect(ui.treeWidget, &QTreeWidget::itemClicked, this, &CCMainWindow::onItemClicked);
    connect(ui.treeWidget, &QTreeWidget::itemExpanded, this, &CCMainWindow::onItemExpanded);
    connect(ui.treeWidget, &QTreeWidget::itemCollapsed, this, &CCMainWindow::onItemCollapsed);
    connect(ui.treeWidget, &QTreeWidget::itemDoubleClicked, this, &CCMainWindow::onItemDoubleClicked);


    //创建根项 QTreeWidgetItem
    QTreeWidgetItem* pRootGroupItem = new QTreeWidgetItem();
    pRootGroupItem->setChildIndicatorPolicy(QTreeWidgetItem::ShowIndicator);

    //为 QTreeWidgetItem 节点绑定一份 “隐形的自定义业务数据”
    pRootGroupItem->setData(0, Qt::UserRole, 0);        //根项 key = <0, Qt::UserRole>,  value = 0

    //创建根项贴纸，设置贴纸信息
    RootContactItem* pItemName = new RootContactItem(true, ui.treeWidget);
    pItemName->setText(QStringLiteral("Yyyy科技"));

    /*必须先调用 addTopLevelItem（或 addChild）将 QTreeWidgetItem 添加到树形控件中，
    然后才能调用 setItemWidget 绑定自定义控件*/
    ui.treeWidget->addTopLevelItem(pRootGroupItem);
    ui.treeWidget->setItemWidget(pRootGroupItem, 0, pItemName);


    //查询数据库，根据用户 employeeID 获取对应 departmentID
    QSqlQuery query;
    query.prepare("SELECT `departmentID` FROM `tab_employees` WHERE `employeeID` = ?");
    query.addBindValue(this->m_empID);
    query.exec();
    query.next();
    int SelfDepID = query.value(0).toInt();

    //查询数据库，获取公司群 departmentID
    query.prepare("SELECT `departmentID` FROM `tab_department` WHERE `department_name` = ?");
    query.addBindValue(QStringLiteral("公司群"));
    query.exec();
    query.next();
    int CompDepID = query.value(0).toInt();

    //添加子项，将用户 部门群 和 公司群 添加上去
    this->addGroupItem(pRootGroupItem, SelfDepID);
    this->addGroupItem(pRootGroupItem, CompDepID);
}


//事件过滤器
bool CCMainWindow::eventFilter(QObject* obj, QEvent* event) {
    if (obj == ui.searchLineEdit)
    {
        //触发 焦点事件
        if (event->type() == QEvent::FocusIn)       // 情况1：搜索输入框获得了焦点（用户点击了搜索框）
        {
            //动态设置搜索栏的激活状态样式
            ui.searchWidget->setStyleSheet(QString(
                // 1. 搜索栏背景变成纯白色
                // 2. 底部添加一条与皮肤颜色一致的边框，突出输入状态
                "QWidget#searchWidget{background-color:rgb(255,255,255);border-bottom:1px solid rgba(%1,%2,%3,100)} "
                // 3. 搜索按钮图标从放大镜切换为删除图标
                "QPushButton#searchBtn{border-image:url(:/Resources/MainWindow/search/main_search_deldown.png)} "
                // 4. 保留按钮的三态交互效果
                "QPushButton#searchBtn:hover{border-image:url(:/Resources/MainWindow/search/main_search_delhighlight.png)} "
                "QPushButton#searchBtn:pressed{border-image:url(:/Resources/MainWindow/search/main_search_delhighdown.png)}"
            )
                //动态注入全局皮肤颜色，让边框颜色跟随主题变化
                .arg(m_colorBackground.red())
                .arg(m_colorBackground.green())
                .arg(m_colorBackground.blue()));
            
            return false;
        }
        else if (event->type() == QEvent::FocusOut)         //情况2：搜索输入框失去了焦点（用户点击了其他地方）
        {
            //还原搜索栏的默认样式
            //调用专门的函数恢复默认背景+放大镜图标
            this->updateSearchStyle();

            return false;
        }
    }

    //交给父类处理其他事件/对象，保证原有逻辑不丢失
    return BasicWindow::eventFilter(obj, event);
}

//事件重写
void CCMainWindow::resizeEvent(QResizeEvent* event) {
    //从动态属性取出用户名（initUserInfo 存入），重新计算省略以适配新宽度
    this->setUserName(this->property("username").toString());

    BasicWindow::resizeEvent(event);
}

void CCMainWindow::mousePressEvent(QMouseEvent* event) {
    if ((qApp->widgetAt(event->pos()) != ui.searchLineEdit) && ui.searchLineEdit->hasFocus()) {
        ui.searchLineEdit->clearFocus();
    }
    else if ((qApp->widgetAt(event->pos()) != ui.lineEdit) && ui.lineEdit->hasFocus()) {
        ui.lineEdit->clearFocus();
    }

    BasicWindow::mousePressEvent(event);
}


//槽函数
void CCMainWindow::onBottonAppClicked() {
    QPushButton* btn = qobject_cast<QPushButton*>(sender());    //通过 sender() 获取发出信号的控件指针
    if (!btn) return;

    const QString name = btn->objectName();
    if (name == "app_skin") {       //根据应用控件名称执行对应方法
        SkinWindow* skinWindow = new SkinWindow;
        skinWindow->show();
    }
}

void CCMainWindow::onItemClicked(QTreeWidgetItem* item, int column) {
    //当 QTreeWidget 发送单击信号，且 QTreeWidgetItem 为根项，则更改根项状态 收起/展开 

    //读取这个项上贴的第一个小纸条（Qt::UserRole）
    //0为根项，1为子项
    bool bIsChild = item->data(0, Qt::UserRole).toBool();
    //判断是否为根项
    if (!bIsChild) {
        item->setExpanded(!item->isExpanded());         //切换展开/收起状态
    }
}

void CCMainWindow::onItemExpanded(QTreeWidgetItem* item) {
    //当根项发送展开信号，同时更改根项贴纸样式

    //先判断是否为根项
    bool bIsChild = item->data(0, Qt::UserRole).toBool();
    if (!bIsChild) {
        //获取根项上的“贴纸”
        RootContactItem* pRootItem = dynamic_cast<RootContactItem*>(ui.treeWidget->itemWidget(item, 0));
        //获取成功，调用贴纸方法
        if (pRootItem) {
            pRootItem->setExpanded(true);
        }
    }
}

void CCMainWindow::onItemCollapsed(QTreeWidgetItem* item) {
    //当根项发送收起信号，同时更改根项贴纸样式
    bool bIsChild = item->data(0, Qt::UserRole).toBool();
    if (!bIsChild) {
        RootContactItem* pRootItem = dynamic_cast<RootContactItem*>(ui.treeWidget->itemWidget(item, 0));
        if (pRootItem) {
            pRootItem->setExpanded(false);
        }
    }
}

void CCMainWindow::onItemDoubleClicked(QTreeWidgetItem* item, int column) {
    //当 QTreeWidget 发送双击信号，且 QTreeWidgetItem 为子项，则创建打开聊天窗口
    
    bool bIsChild = item->data(0, Qt::UserRole).toBool();
    //如果为子项，则添加聊天窗口
    if (bIsChild) {
        //提取子项唯一标识 uid (单聊: employeeID    群聊: departmentID)
        const int uid = item->data(0, Qt::UserRole + 1).toInt();
        WindowManager::getInstance().addNewTalkWindow(uid);
    }
}

void CCMainWindow::onLogoutTriggered() {
    // 向服务端发送注销请求
    TcpClient::getInstance().sendLogout();

    this->doLocalLogout();
}

void CCMainWindow::onKickedOut() {
    QMessageBox::information(this, QStringLiteral("提示"),
        QStringLiteral("您的账号已在其他设备登录，您已被迫下线！"));

    this->doLocalLogout();
}

