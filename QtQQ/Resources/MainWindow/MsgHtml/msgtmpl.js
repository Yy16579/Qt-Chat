//通用版消息模板脚本
//不再为每个员工硬编码 external_<ID> 变量和 recvHtml_<ID> 函数
//改为保存 channel.objects 对象表，通过名字动态取任意发送者的头像模板
var channelObjects = null;


String.prototype.format = function() {
	if(arguments.length == 0) return this;
	var obj = arguments[0];
	var s = this;
	for(var key in obj) {
		s = s.replace(new RegExp("\\{\\{" + key + "\\}\\}", "g"), obj[key]);
	}
	return s;
}

new QWebChannel(qt.webChannelTransport,
	function(channel) {
		//保存完整对象表：C++ 侧在 load 之前注册的所有对象都在此
		channelObjects = channel.objects;
	}
);

//显示自己发送的消息（右侧气泡，模板取 external0）
function appendHtml0(msg){
	$("#placeholder").append(channelObjects.external0.msgRHtmlTmpl.format(msg));
	window.scrollTo(0,document.body.scrollHeight);
};

//显示收到的消息（左侧气泡，objName 形如 "external_10001"，头像取对应发送者的模板）
function recvMsgHtml(objName, msg){
	$("#placeholder").append(channelObjects[objName].msgLHtmlTmpl.format(msg));
	window.scrollTo(0,document.body.scrollHeight);
};
