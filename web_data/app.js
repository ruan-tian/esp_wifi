const ui = {
    // 发送文字到 ESP32 屏幕
    async sendMsg() {
        const input = document.getElementById('msgInput');
        const text = input.value.trim();
        if (!text) return alert("请输入内容");

        try {
            const resp = await fetch('/api/msg', {
                method: 'POST',
                body: text
            });
            if (resp.ok) {
                input.value = '';
                alert("发送成功！请查看设备屏幕");
            }
        } catch (e) {
            alert("发送失败，请检查连接");
        }
    },

    // 获取 WiFi 列表并渲染
    async refreshWiFi() {
        const list = document.getElementById('wifiList');
        list.innerHTML = '<li class="loading">正在扫描...</li>';

        try {
            const resp = await fetch('/api/wifi');
            const data = await resp.json();
            
            list.innerHTML = data.map(item => `
                <li class="wifi-item">
                    <div class="wifi-info">
                        <b>${item.ssid || '[隐藏网络]'}</b>
                        <span>信号强度: ${item.rssi} dBm</span>
                    </div>
                    <div class="rssi">${this.getSignalIcon(item.rssi)}</div>
                </li>
            `).join('');
        } catch (e) {
            list.innerHTML = '<li class="error">无法获取列表</li>';
        }
    },

    getSignalIcon(rssi) {
        if (rssi > -50) return '●●●●';
        if (rssi > -70) return '●●●○';
        return '●○○○';
    }
};

// 页面加载后自动执行一次扫描
window.onload = () => ui.refreshWiFi();