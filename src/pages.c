/* pages.c - the navigation table. */
#include "pages.h"

const UI_PAGE gPages[] = {
  { "概览",     "Home",    "↑↓ 选择   Enter 打开   T 连接图   ← → 换页",
    PageHomeEnter, 0, PageHomeDraw, PageHomeKey, PageHomePoint },
  { "系统信息", "System",  "↑↓ 滚动列表   1-9 直跳页面 · 0 关于   F5 重新采集   F2 导出报告",
    PageSysInfoEnter, 0, PageSysInfoDraw, PageSysInfoKey, PageSysInfoPoint },
  { "系统拓扑", "Topology", "↑↓ 选择   Enter 定位块设备   H 空端口   F5 重扫",
    PageTopoEnter, 0, PageTopoDraw, PageTopoKey, PageTopoPoint },
  { "内存检测", "Memory",  "F5 开始   F6 停止   F7 图案   F8 覆盖   T 颗粒视图",
    PageMemEnter, PageMemLeave, PageMemDraw, PageMemKey, PageMemPoint },
  { "内存拓扑", "DIMM",    "↑↓/PgUp/PgDn/Home/End 选择插槽   Enter 颗粒视图   Esc 返回",
    PageDimmEnter, 0, PageDimmDraw, PageDimmKey, PageDimmPoint },
  { "硬盘检测", "Storage", "↑↓ 选择设备   F5 扫描   F9 扫描全部   F6 停止",
    PageDiskEnter, 0, PageDiskDraw, PageDiskKey, PageDiskPoint },
  { "CPU 检测", "CPU",     "↑↓ 选择   空格 勾选   F5 运行   F7 全选   F8 核心   L 延迟   B 带宽",
    PageCpuEnter, 0, PageCpuDraw, PageCpuKey, PageCpuPoint },
  { "核间延迟", "Latency", "↑↓ 选行   [ ] 换列   N 换插槽   Z 放大   F5 测量   F6 停止",
    PageLatencyEnter, 0, PageLatencyDraw, PageLatencyKey, 0 },
  { "内存带宽", "Bandwidth", "↑↓ 选行   [ ] 换列   N 换内存域   Z 放大   F5 测量   F6 停止",
    PageBwEnter, 0, PageBwDraw, PageBwKey, 0 },
  { "压力测试", "Stress",  "Tab 分组  ↑↓ 选择  空格 勾选  -/= 时长  Enter 开始",
    PageStressEnter, 0, PageStressDraw, PageStressKey, PageStressPoint },
  { "硬盘擦除", "Erase",   "↑↓ 设备   空格 选算法   R 范围   V 校验   Enter 擦除",
    PageEraseEnter, 0, PageEraseDraw, PageEraseKey, PageErasePoint },
  { "工具集",   "Tools",   "↑↓ 选择   Enter 启动   F5 重新扫描",
    PageToolsEnter, 0, PageToolsDraw, PageToolsKey, PageToolsPoint },
  { "关于",     "About",   "1-9 直跳页面 · 0 关于   F10 退出",
    PageAboutEnter, 0, PageAboutDraw, PageAboutKey, PageAboutPoint }
};

const UINT32 gPageCount = sizeof(gPages) / sizeof(gPages[0]);

UINT32 UiPageCount(VOID)
{
  return gPageCount;
}

VOID UiNavigationJump(UINT32 page)
{
  UiSwitchPage(page);
}
