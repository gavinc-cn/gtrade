



# 函数

```c++
QApplication()
loadStyleSheet()
	//- 检索可能的qss文件路径, 并读取文件内容
// 单例配置类
ConfigManager::instance()
	ConfigManager::load()
		YAML::LoadFile()
			//- 读取配置文件
zrt::create_logger2()
// 注册terminate函数
QtTerminate::installTerminateHandlerBoost()
GrpcNetworkManager::connect()
// 显示登录框
LoginDialog loginDialog
	LoginDialog::setupUI()
	
// 登录成功后打开主窗口
MainWindow mainWindow
	MainWindow::setupUI()
		QWidget* centralWidget = new QWidget(this);
    	QHBoxLayout* mainLayout = new QHBoxLayout(centralWidget);
    		MainWindow::setupPages()
    		//- 搭建UI界面
	MainWindow::loadDataDictionary()
		DictService::instance()
			DictService::initializeFallbackDicts()
				//- 当api不可用时, 
		DictService::load()
			emit loaded()
	m_clockTimer = new QTimer(this)
	connect(m_clockTimer, &QTimer::timeout, this, &MainWindow::updateClock)
	QTimer::start()
	MainWindow::updateClock()
		QLabel::setText()

MainWindow::show()
QApplication::exec()
```