let memoryChart = null;

document.addEventListener('DOMContentLoaded', () => {
    fetchReport();
});

function formatBytes(bytes) {
    if (bytes === 0) return '0 B';
    const k = 1024;
    const sizes = ['B', 'KB', 'MB', 'GB'];
    const i = Math.floor(Math.log(bytes) / Math.log(k));
    return parseFloat((bytes / Math.pow(k, i)).toFixed(2)) + ' ' + sizes[i];
}

async function fetchReport() {
    try {
        const res = await fetch('/api/report');
        if (res.ok) {
            const data = await res.json();
            updateDashboard(data);
        }
    } catch (err) {
        console.warn('Initial telemetry fetch skipped:', err);
    }
}

async function runScenario(scenario) {
    const consoleBox = document.getElementById('consoleOutput');
    const loader = document.getElementById('loader');
    
    loader.classList.remove('hidden');
    consoleBox.textContent = `[System] Triggering VortexKV target application with scenario: '${scenario}'...\n[System] Intercepting dynamic allocations via HeapScope Sentinel...`;

    try {
        const res = await fetch('/api/run', {
            method: 'POST',
            headers: { 'Content-Type': 'application/json' },
            body: JSON.stringify({ scenario })
        });

        const data = await res.json();
        loader.classList.add('hidden');

        if (data.success) {
            consoleBox.textContent = data.stdout || 'Execution finished cleanly.';
            if (data.telemetry) {
                updateDashboard(data.telemetry);
            }
        } else {
            consoleBox.textContent = `[Error] ${data.error || 'Execution failed'}`;
        }
    } catch (err) {
        loader.classList.add('hidden');
        consoleBox.textContent = `[Network Error] Failed to communicate with backend server: ${err.message}`;
    }
}

function updateDashboard(telemetry) {
    if (!telemetry || !telemetry.summary) return;

    const summary = telemetry.summary;

    // Update KPIs
    document.getElementById('totalAllocated').textContent = formatBytes(summary.totalAllocatedBytes);
    document.getElementById('totalFreed').textContent = formatBytes(summary.totalFreedBytes);
    document.getElementById('totalLeaked').textContent = formatBytes(summary.totalLeakedBytes);
    document.getElementById('leakCountBadge').textContent = `${summary.leakCount} leak${summary.leakCount === 1 ? '' : 's'} recorded`;

    // Update progress bars
    const allocRatio = summary.totalAllocatedBytes > 0 ? 100 : 0;
    const freedRatio = summary.totalAllocatedBytes > 0 ? Math.min(100, (summary.totalFreedBytes / summary.totalAllocatedBytes) * 100) : 0;
    document.getElementById('allocBar').style.width = `${allocRatio}%`;
    document.getElementById('freedBar').style.width = `${freedRatio}%`;

    // Update Status Badge
    const statusBadge = document.getElementById('statusBadge');
    if (summary.leakCount > 0) {
        statusBadge.textContent = `CRITICAL (${summary.leakCount} LEAKS)`;
        statusBadge.className = 'status-leaking';
    } else {
        statusBadge.textContent = 'CLEAN (NO LEAKS)';
        statusBadge.className = 'status-clean';
    }

    // Update Leaks Table
    const leaksBody = document.getElementById('leaksBody');
    if (!telemetry.leaks || telemetry.leaks.length === 0) {
        leaksBody.innerHTML = `<tr><td colspan="4" class="empty-state">No active memory leaks recorded. Perfect memory hygiene!</td></tr>`;
    } else {
        leaksBody.innerHTML = telemetry.leaks.map(leak => `
            <tr>
                <td style="color: var(--accent-cyan); font-weight: 600;">${leak.address}</td>
                <td style="color: var(--accent-red); font-weight: 700;">${leak.size} B</td>
                <td><span style="background: rgba(255,255,255,0.05); padding: 2px 8px; border-radius: 4px;">${leak.type}</span></td>
                <td style="color: #e2e8f0;">${leak.origin}</td>
            </tr>
        `).join('');
    }

    // Update Chart
    renderChart(telemetry.functions || []);
}

function renderChart(functions) {
    const ctx = document.getElementById('functionChart').getContext('2d');
    
    const labels = functions.map(f => f.name);
    const data = functions.map(f => f.allocatedBytes);

    if (memoryChart) {
        memoryChart.destroy();
    }

    Chart.defaults.color = '#8b949e';
    Chart.defaults.font.family = "'Outfit', sans-serif";

    memoryChart = new Chart(ctx, {
        type: 'bar',
        data: {
            labels: labels,
            datasets: [{
                label: 'Allocated Memory (Bytes)',
                data: data,
                backgroundColor: [
                    'rgba(0, 242, 254, 0.7)',
                    'rgba(79, 172, 254, 0.7)',
                    'rgba(255, 51, 102, 0.7)',
                    'rgba(0, 230, 118, 0.7)',
                    'rgba(255, 145, 0, 0.7)'
                ],
                borderColor: [
                    '#00f2fe',
                    '#4facfe',
                    '#ff3366',
                    '#00e676',
                    '#ff9100'
                ],
                borderWidth: 1,
                borderRadius: 6
            }]
        },
        options: {
            indexAxis: 'y',
            responsive: true,
            maintainAspectRatio: false,
            plugins: {
                legend: { display: false }
            },
            scales: {
                x: {
                    grid: { color: 'rgba(255, 255, 255, 0.05)' },
                    ticks: { color: '#8b949e' }
                },
                y: {
                    grid: { display: false },
                    ticks: { color: '#f0f6fc', font: { weight: '500' } }
                }
            }
        }
    });
}
