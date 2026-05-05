#!/usr/bin/env node
/**
 * GTrade Web Client 启动脚本
 * 设置进程名称为 gtrade_webcli，便于进程管理
 */

import os from 'os';

// 设置进程名称（限制为15个字符以内，以便 pgrep 可以匹配）
process.title = 'gtrade_webcli';

console.log('==========================================');
console.log('   GTrade Strategy Manager Web Client');
console.log('==========================================');
console.log('');
console.log(`Process Name: ${process.title}`);
console.log(`Process PID:  ${process.pid}`);
console.log('');

// 动态导入 Vite
import('vite').then(({ createServer }) => {
  createServer({
    configFile: './vite.config.js',
    server: {
      host: '0.0.0.0',
      port: 46010,
      strictPort: false
    }
  }).then(server => {
    server.listen().then(() => {
      console.log('');
      console.log('服务器已启动，可通过以下地址访问：');
      console.log('');
      console.log('  本机访问:');
      console.log('    http://localhost:3000');
      console.log('');

      // 获取本机 IP
      const interfaces = os.networkInterfaces();
      const addresses = [];
      for (const name of Object.keys(interfaces)) {
        for (const iface of interfaces[name]) {
          if (iface.family === 'IPv4' && !iface.internal) {
            addresses.push(iface.address);
          }
        }
      }

      if (addresses.length > 0) {
        console.log('  其他主机访问:');
        addresses.forEach(addr => {
          console.log(`    http://${addr}:3000`);
        });
        console.log('');
      }

      console.log('按 Ctrl+C 停止服务器');
      console.log('==========================================');
      console.log('');
    });
  }).catch(err => {
    console.error('Failed to start Vite server:', err);
    process.exit(1);
  });
}).catch(err => {
  console.error('Failed to load Vite:', err);
  process.exit(1);
});

// 优雅退出
process.on('SIGINT', () => {
  console.log('\n正在关闭服务器...');
  process.exit(0);
});

process.on('SIGTERM', () => {
  console.log('\n正在关闭服务器...');
  process.exit(0);
});
