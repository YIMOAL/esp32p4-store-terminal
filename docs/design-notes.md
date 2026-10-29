# 服装库存管理系统 — ESP32-P4 前端 & Python 后端维护规范

## 项目架构概览

```
┌──────────────────────────────┐         MQTT (JSON)        ┌──────────────────────────┐
│      ESP32-P4 嵌入式端       │  ──── clothing/req ────>  │     Python 后端          │
│  ┌────────────────────────┐  │  <──── clothing/resp ────  │  ┌────────────────────┐  │
│  │ main/main.c            │  │                            │  │ mqtt_server.py      │  │
│  │ network/mqtt.c/h       │  │  请求: {action, req_id,   │  │   ├─ handle_request │  │
│  │ network/wifi_hosted.c/h│  │       params: {...}}       │  │   └─ _on_message    │  │
│  │ ui/custom/custom.c/h   │  │                            │  │ db.py               │  │
│  │ ui/generated/*          │  │  响应: {req_id, success, │  │   ├─ SQLite CRUD     │  │
│  │ adapter/lvgl_adapter.*  │  │       data/error}         │  │   └─ 数据表定义      │  │
│  │ bsp/display_init.*      │  │                            │  └────────────────────┘  │
│  │ bsp/touch_init.*        │  │                            │  Broker: broker.hivemq.com│
│  └────────────────────────┘  │                            └──────────────────────────┘
└──────────────────────────────┘
```

| 层级 | 端 | 关键文件 | 职责 |
|------|----|----------|------|
| 网络通信 | P4 | `mqtt.c/h` | 构建 JSON 请求、解析响应、回调分发 |
| 网络通信 | Py | `mqtt_server.py` | 接收请求、路由 action、返回响应 |
| 业务逻辑 | P4 | `custom.c` | 请求队列、响应回调、UI 数据绑定 |
| 业务逻辑 | Py | `db.py` | 数据库 CRUD、表结构定义、查询逻辑 |
| UI 表现 | P4 | `ui/generated/` + `ui/custom/` | LVGL 界面渲染与交互 |

---

## 1. 接口一致性

### 1.1 通信协议规范

两端通过 MQTT Topic `clothing/req` / `clothing/resp` 交换 JSON 消息，**action 名称与参数字段名即为接口契约**，任何变更必须双端同步。

**请求格式（P4 → Py）：**

```json
{
  "action": "<动作名>",
  "req_id": 123,
  "params": {
    "date": "2026-05-20",
    "page": 1,
    "page_size": 8
  }
}
```

**响应格式（Py → P4）：**

```json
{
  "req_id": 123,
  "success": true,
  "data": { ... }
}
```

或失败时：

```json
{
  "req_id": 123,
  "success": false,
  "error": "描述信息"
}
```

### 1.2 现有 Action 清单

以下为已实现的 action 列表，**新增/修改 action 时必须更新此表**：

| Action | 方向 | 请求参数 (params) | 响应 data 字段 | P4 处理函数 | Py 处理函数 |
|--------|------|-------------------|----------------|-------------|-------------|
| `ping` | P4→Py | 无 | `"pong"` | — | `handle_request` |
| `get_overview` | P4→Py | 无 | `total_stock`, `today_sales` | `overview_resp_cb` | `db.get_overview` |
| `get_stock_records` | P4→Py | `date`, `page`, `page_size` | `items[]`, `page`, `total_pages` | `stock_records_resp_cb` | `db.get_stock_records_by_date` |
| `add_stock_full` | P4→Py | `date`, `code`, `name`, `brand`, `color`, `size`, `quantity`, `price` | 新记录 | — | `db.add_stock_full` |
| `delete_stock_record` | P4→Py | `id` | success | — | `db.delete_stock_record` |
| `delete_stock_by_code` | P4→Py | `code` | `deleted` (删除条数) | — | `db.delete_stock_record_by_code`（同时清理无售货记录的服装） |
| `get_sell_records` | P4→Py | `date`, `page`, `page_size` | `items[]`, `page`, `total_pages` | `sell_records_resp_cb` | `db.get_sell_records_by_date` |
| `add_sell_full` | P4→Py | `date`, `code`, `name`, `brand`, `color`, `size`, `quantity`, `total_price` | 新记录 | — | `db.add_sell_full` |
| `delete_sell_record` | P4→Py | `id` | success | — | `db.delete_sell_record` |
| `delete_sell_by_code` | P4→Py | `code` | `deleted` (删除条数) | — | `db.delete_sell_record_by_code`（同时清理无入库记录的服装） |
| `get_by_category` | P4→Py | `category`, `page`, `page_size` | `items[]`, `page`, `total_pages` | `check_items_resp_cb` | `db.get_clothing_by_category_paged` |
| `search` | P4→Py | `keyword`, `page`, `page_size` | `items[]`, `page`, `total_pages` | `check_items_resp_cb` | `db.search_clothing_paged` |

### 1.3 变更强制同步策略

**规则：先定义接口，后实现逻辑。** 任何涉及 action 名或字段变更的修改，必须按以下流程执行：

1. **接口定义先行**：在本文件 Action 清单中新增/修改条目，明确字段名、类型、必填/可选
2. **双端同时提交**：P4 端 `mqtt_req_task` 中的参数构建 与 Py 端 `handle_request` 中的参数解析，**必须在同一 PR 中变更**
3. **向后兼容**：新增可选字段时，Py 端用 `.get(key, default)` 取值，P4 端用 `cJSON_GetObjectItem` 判空后取值；**禁止删除或重命名已有字段**，若必须废弃，需保留至少一个版本周期的兼容读取
4. **类型一致**：数值类型两端统一（P4 用 `cJSON_AddNumber`，Py 用 `int`/`float`）；字符串类型统一用 UTF-8 编码

**违反检测**：若 P4 发送了 Py 未识别的 action，Py 返回 `{"success": false, "error": "unknown action: ..."}`，P4 端 `mqtt_process_response` 中无匹配回调将打印 `No callback for req_id`。出现此类日志即为接口未同步。

---

## 2. 状态同步

### 2.1 请求-响应关联机制

P4 端通过 `req_id` 将请求与响应关联，回调注册在 `s_pending[]` 数组中（最多 8 个并发）：

```
P4: mqtt_send_request(action, params, callback)
    → 注册 {req_id, cb} 到 s_pending[]
    → 发布 JSON 到 clothing/req

Py: _on_message → handle_request → 返回 {req_id, ...}
    → 发布 JSON 到 clothing/resp

P4: mqtt_process_response → 按 resp.req_id 查找 s_pending[] → 调用对应 cb
```

### 2.2 状态一致性要求

| 场景 | P4 端职责 | Py 端职责 |
|------|-----------|-----------|
| **新增记录** | 提交后立即 `post_request(REQ_xxx_RECORDS, date, page)` 刷新列表 | 确保写入事务提交后才返回 success=true |
| **删除记录** | 提交后刷新当前页；若当前页已无数据则回退 page | 返回 success=false 时 P4 不刷新 |
| **翻页查询** | 维护 `s_xxx_page` / `s_xxx_total_pages`，取消上一页未返回的请求 | 响应中必须包含 `page` 和 `total_pages` |
| **页面切换** | `screen_monitor_cb` 检测切屏，`cancel_child_requests()` 取消所有待处理请求 | 无需特殊处理，Py 无状态 |
| **每日同步** | SNTP 时钟检测跨日，自动 `REQ_SYNC_SELL_RECORDS` | 重新拉取当天数据 |

### 2.3 并发与竞态防护

- **P4 端**：同一类型请求（如 `REQ_STOCK_RECORDS`）使用 `s_stock_req_id` 追踪，新请求发出前 `mqtt_cancel_callback` 取消旧回调，防止旧响应覆盖新数据
- **Py 端单连接事务规则**：SQLite WAL 模式仅允许一个写连接，`db.py` 中所有复合写操作（如 `add_stock_record` = INSERT + UPDATE 库存）必须在**同一连接内完成**，禁止在事务中调用 `update_stock` 等会新建连接的辅助函数，否则会触发 `database is locked`
- **MQTT 分片**：P4 端 `mqtt_event_callback` 实现消息重组缓冲区，大 JSON 响应分多片到达时正确拼接

### 2.4 删除操作的数据完整性保护

按款号删除记录时，`db.py` 采用以下策略防止级联数据丢失：

| 操作 | 行为 | 保护机制 |
|------|------|----------|
| `delete_stock_record_by_code` | 删除所有入库记录 + 回退库存 | 仅当该服装**无售货记录**时才删除 `clothing` 和 `clothing_stock`，避免级联删除售货数据 |
| `delete_sell_record_by_code` | 删除所有售货记录 + 恢复库存 | 仅当该服装**无入库记录**时才删除 `clothing` 和 `clothing_stock`，避免级联删除入库数据 |
| 服装无记录时 | 两种删除均会清理孤儿 `clothing` 条目 | 解决因 `add_stock_full` 非原子性导致的孤儿数据问题 |

**注意**：若服装同时有入库和售货记录，删除其中一种记录不会删除 `clothing` 基本信息，另一种记录仍然安全。

### 2.5 表项变更同步检查清单

修改 P4 端下拉选项（如款式、牌子、颜色）时：

- [ ] 确认 `db.py` `_seed_data` 中的种子数据是否需要同步更新
- [ ] 确认新增字段在 `db.py` 对应的 INSERT/SELECT 语句中是否存在
- [ ] 确认 P4 端 `custom.c` 中 `xxx_resp_cb` 响应解析的 `json_str/json_int/json_double` 字段名与 Py 端返回的 JSON key 一致
- [ ] 确认 `chinese.txt` 字体文件是否包含新增的中文字符

---

## 3. 依赖与编译

### 3.1 P4 端编译部署流程

```
1. 修改源码
2. idf.py build                    # 编译固件
3. idf.py -p COMx flash            # 烧录
4. idf.py -p COMx monitor          # 查看日志
```

**关键依赖：**

| 依赖 | 版本/来源 | 说明 |
|------|-----------|------|
| ESP-IDF | v5.5.3 | 框架版本，不建议随意升级 |
| LVGL | 9.x (managed_component) | GUI 框架，通过 idf_component.yml 管理 |
| cJSON | ESP-IDF 内置 | JSON 解析，无需额外安装 |
| esp_mqtt | ESP-IDF 内置 | MQTT 客户端 |

**GUI Guider 代码再生注意事项：**

- `ui/generated/` 目录由 NXP GUI Guider 工具生成，**每次 Generate Code 会完整覆盖**
- `ui/custom/` 为手写业务代码，不受 GUI Guider 影响
- 字体文件 `guider_fonts/lv_font_ZiTiQuanWeiJunHeiW22_25.c` 再生后需执行替换脚本（见下方 3.3 节）
- GUI Guider 在中文 Windows 上输出 GBK 编码文件，脚本处理时需注意编码

### 3.2 敏感配置管理

WiFi 密码、MQTT 连接信息等敏感数据统一存放在独立配置文件中，**不会提交到 Git 仓库**。

**文件说明：**

|| 文件 | 说明 | 是否提交 Git |
||------|------|-------------|
|| `main/config/app_config.h` | 配置总头文件，定义默认占位值，通过 `__has_include` 加载私有配置 | ✅ 是 |
|| `main/config/app_config_private.h` | **敏感配置**，包含真实 WiFi/MQTT 凭证 | ❌ 否（.gitignore 排除）|
|| `main/config/app_config_private.h.example` | 私有配置模板，供新开发者参考 | ✅ 是 |

**首次使用步骤：**

```bash
# 1. 复制模板文件
cp main/config/app_config_private.h.example main/config/app_config_private.h

# 2. 编辑填入实际配置
#    修改 WIFI_SSID、WIFI_PASSWORD、MQTT_BROKER、MQTT_USER、MQTT_PASS 等
```

**加载机制：** `app_config.h` 使用 `__has_include` 检测 `app_config_private.h` 是否存在：
- 存在 → 包含私有配置，其中 `#define` 覆盖默认值
- 不存在 → 使用 `app_config.h` 中的默认占位值（编译通过但无法正常连接网络）

**代码引用：** `wifi_hosted.h` 和 `mqtt.h` 已改为 `#include "app_config.h"`，不再硬编码敏感信息。

### 3.3 Python 端环境与兼容性

```
1. cd d:\python-project\mcp-mouse
2. pip install paho-mqtt          # MQTT 客户端库
3. python mqtt_server.py          # 启动后端服务
```

**依赖清单：**

| 包 | 版本 | 说明 |
|----|------|------|
| Python | >= 3.10 | 使用 `X \| None` 类型语法 |
| paho-mqtt | >= 2.0 | MQTT 客户端，v2 API (`CallbackAPIVersion.VERSION2`) |
| sqlite3 | 标准库 | 数据库，零配置，DB 文件自动创建 |

**兼容性检查步骤：**

1. 修改 `db.py` 表结构后，需确认 `init_db()` 中 `CREATE TABLE IF NOT EXISTS` 是否兼容旧表结构
2. 修改 `mqtt_server.py` 的 action 签名后，需确认 P4 端 `custom.c` 的 `mqtt_req_task` 中参数字段是否匹配
3. 新增 Python 依赖后，更新本文档依赖清单并通知团队

### 3.3 字体替换脚本（GUI Guider 再生后执行）

GUI Guider 生成的 `guider_fonts/lv_font_ZiTiQuanWeiJunHeiW22_25.c` 仅包含界面静态文本的字符，缺少动态内容的汉字。需将 `guider_customer_fonts` 中的完整字体替换过去：

```python
# replace_font.py — 每次 GUI Guider Generate Code 后执行
f = open('main/ui/generated/guider_customer_fonts/lv_customer_font_ZiTiQuanWeiJunHeiW22_25.c',
         'r', encoding='utf-8')
c = f.read()
f.close()

c = c.replace('lv_customer_font_ZiTiQuanWeiJunHeiW22_25',
              'lv_font_ZiTiQuanWeiJunHeiW22_25') \
     .replace('LV_CUSTOMER_FONT_ZITIQUANWEIJUNHEIW22_25',
              'LV_FONT_ZITIQUANWEIJUNHEIW22_25')

f2 = open('main/ui/generated/guider_fonts/lv_font_ZiTiQuanWeiJunHeiW22_25.c',
          'w', encoding='utf-8')
f2.write(c)
f2.close()
print('Font replacement done.')
```

**字符更新流程**：新增中文字符时 → 更新 `C:\Users\Lenovo\Desktop\chinese.txt` → GUI Guider 导入该文件到字体资源 → Generate Code → 执行上述脚本

---

## 4. 影响评估

### 4.1 修改影响分级

| 级别 | 范围 | 示例 | 必须动作 |
|------|------|------|----------|
| **P4 单端** | 仅影响嵌入式端 UI/交互 | 调整对话框布局、修改键盘避让偏移量、变更下拉选项顺序 | 仅修改 `ui/custom/custom.c`，无需改 Py |
| **Py 单端** | 仅影响后端数据处理 | 优化 SQL 查询性能、调整分页 page_size 默认值 | 仅修改 `db.py`，确保响应格式不变 |
| **双端联动** | 涉及通信协议变更 | 新增 action、修改请求/响应字段名或类型 | 必须双端同一 PR 同步修改 |

### 4.2 双端联动判定标准

**以下任一条件成立即为双端联动修改：**

- [x] 新增或删除 `mqtt_send_request` 调用中的 action 字符串
- [x] 修改请求 JSON 中 `params` 对象的字段名或字段类型
- [x] 修改响应 JSON 中 `data` 对象的结构（字段增删、类型变化）
- [x] P4 端新增 `xxx_resp_cb` 中解析了之前未使用的响应字段
- [x] Py 端 `handle_request` 中新增了之前不存在的必填参数

**以下为单端修改：**

- [ ] 修改 P4 端 UI 布局（控件位置、颜色、字号）
- [ ] 修改 P4 端事件回调中的 UI 交互逻辑（不涉及 MQTT 通信）
- [ ] 修改 Py 端数据库查询实现（SQL 优化，返回格式不变）
- [ ] 修改 Py 端错误处理/日志格式

### 4.3 变更检查清单（每次修改必过）

```
□ 本次修改涉及哪些文件？（P4 / Py / 双端）
□ 若涉及 MQTT 通信，action 名是否为已有？若是新增，是否已更新 Action 清单？
□ 请求参数字段名是否与对端解析代码一致？
    P4: mqtt_req_task() 中的 cJSON_AddXxxToObject() 的 key
    Py: handle_request() 中的 req.get() 的 key
□ 响应数据字段名是否与对端解析代码一致？
    P4: xxx_resp_cb() 中的 json_str/json_int/json_double() 的 key
    Py: handle_request() 返回的 dict key
□ 新增中文字符是否已更新 chinese.txt 并重新生成字体？
□ 是否需要更新本文档的 Action 清单？
```

### 4.4 常见故障与未同步症状

| 现象 | 可能原因 | 排查位置 |
|------|----------|----------|
| P4 日志 `No callback for req_id=X` | P4 发了请求但回调已被取消/超时，或 req_id 不匹配 | P4: `mqtt_process_response` |
| Py 日志 `unknown action: XXX` | P4 新增了 action 但 Py 未实现 | Py: `handle_request` 的 else 分支 |
| Py 日志 `database is locked` | `db.py` 复合写操作在事务中嵌套调用了开新连接的函数 | Py: `db.py` 中的 `add_stock_record` / `add_sell_full` 等，确保库存更新内联在同一连接 |
| P4 界面显示空白/默认值 | Py 响应字段名与 P4 解析不一致，`json_str` 返回默认值 | 对比 P4 `xxx_resp_cb` 与 Py 返回 dict |
| P4 日志 `Failed to parse JSON` | 响应 JSON 格式损坏（分片丢失或编码异常） | P4: `mqtt_process_response` |
| 中文显示为方框/乱码 | 字体文件缺少字符 或 GUI Guider 覆盖后未执行替换脚本 | `guider_fonts/` 字体文件 |
| 删除款号后另一类记录消失 | 级联删除导致（已修复，见 2.4 节） | Py: `delete_stock_record_by_code` / `delete_sell_record_by_code` |

---

## 附录：目录结构

```
12_ESP32P4_TEST/
├── CMakeLists.txt                  # 顶层 CMake
├── partitions.csv                   # 分区表
├── sdkconfig / sdkconfig.defaults   # ESP-IDF 配置
├── main/
│   ├── main.c                       # 入口：Wi-Fi + MQTT + UI 初始化
│   ├── CMakeLists.txt
│   ├── idf_component.yml            # 组件依赖声明
│   ├── bsp/
│   │   ├── display_init.c/h         # LCD 面板初始化 (MIPI-DSI)
│   │   └── touch_init.c/h           # GT911 触摸驱动
│   ├── adapter/
│   │   └── lvgl_adapter.c/h         # LVGL 显示/触摸注册
│   ├── network/
│   │   ├── wifi_hosted.c/h          # Wi-Fi Hosted 模式
│   │   └── mqtt.c/h                 # MQTT 客户端 + 请求/响应管理
│   ├── config/
│   │   ├── app_config.h             # 配置总头文件（含默认占位值）
│   │   ├── app_config_private.h     # ★ 敏感配置（WiFi/MQTT凭证，已gitignore）
│   │   └── app_config_private.h.example  # 敏感配置模板
│   └── ui/
│       ├── custom/
│       │   └── custom.c/h           # ★ 业务逻辑核心（请求队列、回调、UI交互）
│       └── generated/                # GUI Guider 自动生成（勿手动修改）
│           ├── gui_guider.c/h       # 全局 UI 结构体 + 键盘创建
│           ├── setup_scr_*.c        # 各屏幕布局
│           ├── guider_fonts/        # 代码引用的字体文件（需替换脚本）
│           └── guider_customer_fonts/ # GUI Guider 生成的完整字体（替换源）

d:\python-project\mcp-mouse\
├── mqtt_server.py                   # ★ MQTT 请求路由 + action 分发
├── db.py                            # ★ 数据库 CRUD + 表结构定义
└── clothing_store.db                # SQLite 数据库文件（自动生成）
```
