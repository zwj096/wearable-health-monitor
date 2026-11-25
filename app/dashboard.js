'use strict';

const $ = id => document.getElementById(id);
const labels = {nominal: '正常静息', motion: '运动干扰', nocontact: '无接触'};
const stateNotes = {
  0: '正在初始化',
  1: '未满足接触判据，心率暂不可用',
  2: '正在积累完整的 8 秒分析窗口',
  3: '质量门控通过，心率有效',
  4: '信号质量未通过门控，心率暂不可用',
  5: '处于充电状态，心率暂不可用',
  6: '传感器或通信异常，心率暂不可用',
};
let currentPayload = null;

function setText(id, value) {
  $(id).textContent = value;
}

function validHeartRate(row) {
  return row.heart_rate_valid === true && row.state === 3 && (row.flags & 1) !== 0 &&
    Number.isFinite(row.heart_rate_bpm) && row.heart_rate_bpm > 0;
}

function heartRateText(row) {
  return validHeartRate(row) ? row.heart_rate_bpm.toFixed(1) + ' BPM' : '-- BPM';
}

function timeText(timestampMs) {
  return (timestampMs / 1000).toFixed(2) + ' s';
}

function clearResults() {
  currentPayload = null;
  for (const [id, value] of Object.entries({hr: '-- BPM', temp: '-- °C', motion: '-- mg',
    quality: '-- %', battery: '-- mV', sequence: '--', flags: '--', modeLabel: '--',
    packets: '--', timestamp: '--'})) setText(id, value);
  $('chartLine').replaceChildren();
  $('chartDots').replaceChildren();
  $('gridLines').replaceChildren();
  setText('chartMeta', '8 s 分析窗口 / 批量结果');
  setText('empty', '运行场景后显示仿真趋势');
  $('empty').style.display = 'grid';
  $('rows').innerHTML = '<tr><td colspan="8">暂无数据</td></tr>';
  $('download').disabled = true;
}

function renderChart(rows) {
  const width = Math.max(260, $('chart').clientWidth);
  const height = Math.max(180, $('chart').clientHeight);
  $('chart').setAttribute('viewBox', `0 0 ${width} ${height}`);
  const points = rows.filter(validHeartRate);
  const start = rows[0].timestamp_ms;
  const end = rows[rows.length - 1].timestamp_ms;
  const rates = points.map(row => row.heart_rate_bpm);
  const low = rates.length ? Math.max(0, Math.floor((Math.min(...rates) - 8) / 10) * 10) : 40;
  const high = rates.length ? Math.max(low + 20, Math.ceil((Math.max(...rates) + 8) / 10) * 10) : 100;
  const left = 40, right = width - 22, top = 26, bottom = height - 42;
  const xFor = timestamp => end === start ? (left + right) / 2 : left + (right - left) * (timestamp - start) / (end - start);
  const yFor = bpm => bottom - (bottom - top) * (bpm - low) / (high - low);
  const segments = [];
  let segment = [];
  for (const row of rows) {
    if (validHeartRate(row)) {
      segment.push(`${xFor(row.timestamp_ms).toFixed(1)},${yFor(row.heart_rate_bpm).toFixed(1)}`);
    } else if (segment.length) {
      segments.push(segment);
      segment = [];
    }
  }
  if (segment.length) segments.push(segment);
  $('chartLine').innerHTML = segments.map(values => `<polyline class="line" points="${values.join(' ')}"/>`).join('');
  $('chartDots').innerHTML = points.map(row => `<circle class="dot" cx="${xFor(row.timestamp_ms).toFixed(1)}" cy="${yFor(row.heart_rate_bpm).toFixed(1)}" r="4"/>`).join('');
  const horizontal = [0, 0.5, 1].map(fraction => {
    const y = top + (bottom - top) * fraction;
    return `<line class="gridline" x1="${left}" y1="${y}" x2="${right}" y2="${y}"/><text class="axis" x="8" y="${y + 4}">${(high - (high - low) * fraction).toFixed(0)}</text>`;
  }).join('');
  const times = start === end ? [start] : [start, (start + end) / 2, end];
  const ticks = times.map(timestamp => `<text class="axis" text-anchor="middle" x="${xFor(timestamp)}" y="${height - 15}">${timeText(timestamp)}</text>`).join('');
  $('gridLines').innerHTML = horizontal + ticks + '<text class="axis" x="8" y="14">BPM</text>';
  setText('chartMeta', `${timeText(start)} – ${timeText(end)} / ${points.length} 个有效结果`);
  setText('empty', '当前场景暂无有效心率');
  $('empty').style.display = points.length ? 'none' : 'grid';
}

function renderPayload(data) {
  const latest = data.latest;
  setText('hr', heartRateText(latest));
  setText('temp', latest.temperature_c.toFixed(2) + ' °C');
  setText('motion', latest.motion_mg + ' mg');
  setText('quality', latest.quality + ' %');
  setText('battery', latest.battery_mv + ' mV');
  setText('state', latest.state_label);
  setText('stateNote', stateNotes[latest.state] || '未知状态');
  if (latest.flags & 16) setText('stateNote', '采样时间不连续，正在重新积累完整分析窗口');
  $('orb').dataset.state = String(latest.state);
  setText('sequence', latest.sequence);
  setText('flags', '0x' + latest.flags.toString(16).padStart(2, '0').toUpperCase());
  setText('modeLabel', labels[data.mode] || data.mode);
  setText('packets', data.rows.length + ' 个');
  setText('timestamp', timeText(latest.timestamp_ms));
  renderChart(data.rows);
  const body = document.createDocumentFragment();
  for (const row of data.rows.slice().reverse()) {
    const tr = document.createElement('tr');
    const values = [row.sequence, timeText(row.timestamp_ms), row.state_label, heartRateText(row),
      row.temperature_c.toFixed(2) + ' °C', row.quality + '%', row.motion_mg + ' mg', row.battery_mv + ' mV'];
    for (const value of values) {
      const td = document.createElement('td');
      td.textContent = value;
      tr.append(td);
    }
    body.append(tr);
  }
  $('rows').replaceChildren(body);
  currentPayload = data;
  $('download').disabled = false;
}

async function run() {
  const mode = $('mode').value;
  const seconds = Number($('seconds').value);
  clearResults();
  if (!Number.isInteger(seconds) || seconds < 8 || seconds > 60) {
    setText('hint', '请输入 8 到 60 之间的整数时长');
    setText('state', '参数有误');
    setText('stateNote', '修改时长后重新运行');
    $('orb').dataset.state = 'error';
    return;
  }
  for (const id of ['run', 'mode', 'seconds']) $(id).disabled = true;
  setText('hint', '正在生成离线仿真结果…');
  setText('state', '运行中');
  setText('stateNote', '正在计算整段场景');
  $('orb').dataset.state = 'idle';
  try {
    const response = await fetch(`/api/run?mode=${encodeURIComponent(mode)}&seconds=${seconds}`);
    const data = await response.json();
    if (!response.ok) throw new Error(data.error || '运行失败');
    if (!Array.isArray(data.rows) || !data.rows.length || !data.latest) throw new Error('没有可显示的结果');
    renderPayload(data);
    setText('hint', `${labels[data.mode] || data.mode} · ${seconds} s 仿真已完成`);
  } catch (error) {
    clearResults();
    setText('hint', error.message);
    setText('state', '运行失败');
    setText('stateNote', '本次没有可用结果，请检查仿真程序后重试');
    $('orb').dataset.state = 'error';
  } finally {
    for (const id of ['run', 'mode', 'seconds']) $(id).disabled = false;
  }
}

function downloadCsv() {
  if (!currentPayload) return;
  const columns = ['mode', 'sequence', 'timestamp_ms', 'state', 'heart_rate_bpm',
    'heart_rate_bpm_raw', 'heart_rate_valid', 'temperature_c', 'quality', 'motion_mg', 'battery_mv', 'flags'];
  const escape = value => value === null || value === undefined ? '' : `"${String(value).replaceAll('"', '""')}"`;
  const text = '\uFEFF' + [columns.join(','), ...currentPayload.rows.map(row => columns.map(key => escape(row[key])).join(','))].join('\r\n') + '\r\n';
  const url = URL.createObjectURL(new Blob([text], {type: 'text/csv;charset=utf-8'}));
  const link = document.createElement('a');
  link.href = url;
  link.download = `wristsense_${currentPayload.mode}_${currentPayload.latest.timestamp_ms}ms.csv`;
  link.click();
  setTimeout(() => URL.revokeObjectURL(url), 1000);
}

$('run').addEventListener('click', run);
$('download').addEventListener('click', downloadCsv);
window.addEventListener('resize', () => {
  if (currentPayload) renderChart(currentPayload.rows);
});
run();
