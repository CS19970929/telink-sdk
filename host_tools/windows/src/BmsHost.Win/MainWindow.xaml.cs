using System.Collections.ObjectModel;
using System.ComponentModel;
using System.Globalization;
using System.IO.Ports;
using System.Text;
using System.Windows;
using System.Windows.Threading;
using BmsHost.Core.Models;
using BmsHost.Core.Services;
using BmsHost.Core.Transport;
using BmsHost.Win.Models;
using BmsHost.Win.Services;
using BmsHost.Win.Transport;
using Microsoft.Win32;

namespace BmsHost.Win;

public partial class MainWindow : Window
{
    private readonly DispatcherTimer _pollTimer;
    private readonly SemaphoreSlim _operationGate = new(1, 1);
    private readonly ObservableCollection<CellRow> _cells = new();
    private readonly ObservableCollection<ProtectionParameterRow> _protectionRows = new();
    private IBmsTransport? _transport;
    private BmsClient? _client;
    private BleBmsTransport? _bleTransport;
    private CancellationTokenSource? _otaCts;
    private DateTime _lastPoll = DateTime.MinValue;
    private bool _otaInProgress;
    private string? _otaFile;

    public MainWindow()
    {
        InitializeComponent();
        CellsGrid.ItemsSource = _cells;
        ProtectionGrid.ItemsSource = _protectionRows;
        RefreshSerialPorts();
        _pollTimer = new DispatcherTimer { Interval = TimeSpan.FromMilliseconds(200) };
        _pollTimer.Tick += PollTimer_Tick;
        _pollTimer.Start();
        UpdateControls();
    }

    private bool IsConnected => _transport?.IsConnected == true;

    private void RefreshSerialPorts()
    {
        var selected = SerialPortCombo.SelectedItem as string;
        var ports = SerialPort.GetPortNames().OrderBy(x => x, StringComparer.OrdinalIgnoreCase).ToArray();
        SerialPortCombo.ItemsSource = ports;
        if (selected is not null && ports.Contains(selected)) SerialPortCombo.SelectedItem = selected;
        else if (ports.Length > 0) SerialPortCombo.SelectedIndex = 0;
    }

    private void RefreshSerial_Click(object sender, RoutedEventArgs e) => RefreshSerialPorts();

    private async void SerialConnect_Click(object sender, RoutedEventArgs e)
    {
        try
        {
            if (_transport is SerialBmsTransport && IsConnected) { await DisconnectCurrentAsync(); return; }
            if (IsConnected) await DisconnectCurrentAsync();
            var port = SerialPortCombo.SelectedItem as string;
            if (string.IsNullOrWhiteSpace(port)) throw new InvalidOperationException("Select a COM port first.");
            var baudText = (BaudCombo.SelectedItem as System.Windows.Controls.ComboBoxItem)?.Content?.ToString() ?? "115200";
            var transport = new SerialBmsTransport(port, int.Parse(baudText, CultureInfo.InvariantCulture));
            transport.Disconnected += Transport_Disconnected;
            await transport.ConnectAsync();
            AttachTransport(transport);
            Log($"Connected: {transport.Name}");
            await TryInitialReadAsync();
        }
        catch (Exception ex) { LogError(ex); await DisconnectCurrentAsync(); }
    }

    private async void BleScan_Click(object sender, RoutedEventArgs e)
    {
        BleScanButton.IsEnabled = false;
        try
        {
            Log("BLE scan started (4 s)...");
            var devices = await BleBmsTransport.ScanAsync(TimeSpan.FromSeconds(4));
            BleDeviceCombo.ItemsSource = devices;
            if (devices.Count > 0) BleDeviceCombo.SelectedIndex = 0;
            Log($"BLE scan complete: {devices.Count} device(s).");
        }
        catch (Exception ex) { LogError(ex); }
        finally { BleScanButton.IsEnabled = true; }
    }

    private async void BleConnect_Click(object sender, RoutedEventArgs e)
    {
        try
        {
            if (_transport is BleBmsTransport && IsConnected) { await DisconnectCurrentAsync(); return; }
            if (IsConnected) await DisconnectCurrentAsync();
            if (BleDeviceCombo.SelectedItem is not BleDeviceItem item)
                throw new InvalidOperationException("Scan and select a BLE device first.");
            var transport = new BleBmsTransport();
            transport.Disconnected += Transport_Disconnected;
            await transport.ConnectAsync(item.Address);
            _bleTransport = transport;
            AttachTransport(transport);
            Log($"Connected: {transport.Name}; NUS ready; OTA={(transport.HasOta ? "available" : "not found")}");
            await TryInitialReadAsync();
        }
        catch (Exception ex) { LogError(ex); await DisconnectCurrentAsync(); }
    }

    private void AttachTransport(IBmsTransport transport)
    {
        _transport = transport;
        _client = new BmsClient(transport);
        _client.Modbus.RetryCount = 3;
        _client.Modbus.RequestTimeout = TimeSpan.FromMilliseconds(900);
        _client.Modbus.RetryDelay = TimeSpan.FromMilliseconds(180);
        _lastPoll = DateTime.MinValue;
        ConnectionStatusText.Text = $"Connected - {transport.Name}";
        UpdateControls();
    }

    private async Task TryInitialReadAsync()
    {
        await ReadIdentityInternalAsync();
        await RefreshSnapshotInternalAsync();
    }

    private async Task DisconnectCurrentAsync()
    {
        _otaCts?.Cancel();
        var old = _transport;
        _transport = null; _client = null; _bleTransport = null;
        if (old is not null)
        {
            old.Disconnected -= Transport_Disconnected;
            try { await old.DisposeAsync(); } catch { }
        }
        ConnectionStatusText.Text = "Disconnected";
        UpdateControls();
    }

    private void Transport_Disconnected(object? sender, EventArgs e)
    {
        _ = Dispatcher.InvokeAsync(async () =>
        {
            Log("Transport disconnected.");
            await DisconnectCurrentAsync();
        });
    }

    private async void PollTimer_Tick(object? sender, EventArgs e)
    {
        if (!IsConnected || _client is null || _otaInProgress || AutoRefreshCheck.IsChecked != true) return;
        if (!int.TryParse(PollIntervalText.Text, out var intervalMs)) intervalMs = 1000;
        intervalMs = Math.Clamp(intervalMs, 200, 60000);
        if ((DateTime.UtcNow - _lastPoll).TotalMilliseconds < intervalMs) return;
        if (!await _operationGate.WaitAsync(0)) return;
        try { _lastPoll = DateTime.UtcNow; await RefreshSnapshotInternalAsync(); }
        catch (Exception ex) { Log($"Poll: {ex.Message}"); }
        finally { _operationGate.Release(); }
    }

    private async Task RefreshSnapshotInternalAsync()
    {
        if (_client is null) return;
        var s = await _client.ReadSnapshotAsync();
        PackVoltageText.Text = $"{s.PackVoltageV:F2} V";
        CurrentText.Text = $"{s.CurrentA:+0.0;-0.0;0.0} A";
        SocSohText.Text = $"SOC {s.SocPercent}% / SOH {s.SohPercent}%";
        CellExtremaText.Text = $"{s.CellMaxV:F3} / {s.CellMinV:F3} V";
        CellDeltaText.Text = $"{s.CellDeltaV * 1000:F0} mV";
        TempsText.Text = $"{s.Temperature1C:F1} / {s.Temperature2C:F1} °C (max {s.TemperatureMaxC:F1})";
        AfeText.Text = $"online={s.AfeOnline}, init={s.AfeInitOk}, D115=0x{s.AfeStatusRaw:X4}, D116=0x{s.AfeFlagsRaw:X4}";
        LastUpdateText.Text = s.Timestamp.ToString("HH:mm:ss.fff");
        var charge = (s.MosFlags & (1 << 4)) != 0;
        var discharge = (s.MosFlags & (1 << 5)) != 0;
        MosText.Text = $"CHG={(charge ? "ON" : "OFF")}  DSG={(discharge ? "ON" : "OFF")}  flags=0x{s.MosFlags:X4}";
        ProtectText.Text = $"L1=0x{s.ProtectionL1:X4}  L2=0x{s.ProtectionL2:X4}  L3=0x{s.ProtectionL3:X4}  Active=0x{s.ProtectionActive:X4}";
        _cells.Clear();
        for (var i = 0; i < s.CellVoltagesV.Count; i++) _cells.Add(new CellRow { Index = i + 1, VoltageV = s.CellVoltagesV[i] });
        DiagnosticsText.Text = BuildDiagnostics(s);
    }

    private static string BuildDiagnostics(BmsSnapshot s)
    {
        var sb = new StringBuilder();
        sb.AppendLine($"AFE status       : 0x{s.AfeStatusRaw:X4}");
        sb.AppendLine($"AFE flags        : 0x{s.AfeFlagsRaw:X4}");
        sb.AppendLine($"AFE fault bits   : 0x{s.AfeFaultBits:X8}");
        sb.AppendLine($"Protect L1/L2/L3 : {s.ProtectionL1:X4}/{s.ProtectionL2:X4}/{s.ProtectionL3:X4}");
        sb.AppendLine($"Protect active   : 0x{s.ProtectionActive:X4}");
        sb.AppendLine($"MOS flags        : 0x{s.MosFlags:X4}");
        sb.AppendLine($"Last MOS error   : {s.LastMosError}");
        sb.AppendLine($"Serial PM flags  : 0x{s.SerialPmFlags:X4}");
        sb.AppendLine($"Serial sleep/wake: {s.SerialSleepCount}/{s.SerialWakeCount}");
        sb.AppendLine($"Serial PM variant: {s.SerialPmVariant}");
        sb.AppendLine($"Serial PM config : 0x{s.SerialPmConfigFlags:X4}");
        return sb.ToString();
    }

    private async void RefreshNow_Click(object sender, RoutedEventArgs e) => await RunExclusiveAsync(RefreshSnapshotInternalAsync);
    private async void MosOn_Click(object sender, RoutedEventArgs e) => await RunExclusiveAsync(async () => { if (_client is null) return; await _client.SetMosAsync(true); Log("MOS ON command sent."); await RefreshSnapshotInternalAsync(); });
    private async void MosOff_Click(object sender, RoutedEventArgs e) => await RunExclusiveAsync(async () => { if (_client is null) return; await _client.SetMosAsync(false); Log("MOS OFF command sent."); await RefreshSnapshotInternalAsync(); });

    private async void ReadParameters_Click(object sender, RoutedEventArgs e)
    {
        await RunExclusiveAsync(async () =>
        {
            if (_client is null) return;
            var rows = await _client.ReadProtectionParametersAsync();
            _protectionRows.Clear(); foreach (var row in rows) _protectionRows.Add(row);
            Log("Read 13 protection parameter groups.");
        });
    }

    private async void WriteParameters_Click(object sender, RoutedEventArgs e)
    {
        if (MessageBox.Show(this, "Write all protection parameters to the BMS? Verify thresholds before continuing.", "Confirm parameter write", MessageBoxButton.YesNo, MessageBoxImage.Warning) != MessageBoxResult.Yes) return;
        await RunExclusiveAsync(async () =>
        {
            if (_client is null) return;
            ProtectionGrid.CommitEdit();
            await _client.WriteProtectionParametersAsync(_protectionRows.ToArray());
            Log("Protection parameters written. Allow >2 s before removing power so firmware persistence can complete.");
        });
    }

    private async void ReadIdentity_Click(object sender, RoutedEventArgs e) => await RunExclusiveAsync(ReadIdentityInternalAsync);

    private async Task ReadIdentityInternalAsync()
    {
        if (_client is null) return;
        var info = await _client.ReadDeviceInfoAsync();
        SerialNumberText.Text = info.SerialNumber; HardwareVersionText.Text = info.HardwareVersion;
        SoftwareVersionText.Text = info.SoftwareVersion; BluetoothNameText.Text = info.BluetoothName;
        Log($"Identity: {info.SerialNumber} / {info.HardwareVersion} / {info.SoftwareVersion} / {info.BluetoothName}");
    }

    private async void SetBleName_Click(object sender, RoutedEventArgs e)
    {
        await RunExclusiveAsync(async () =>
        {
            if (_client is null) return;
            await _client.SetBluetoothNameAsync(BluetoothNameText.Text.Trim());
            Log("Bluetooth name written.");
            await ReadIdentityInternalAsync();
        });
    }

    private void ChooseOtaFile_Click(object sender, RoutedEventArgs e)
    {
        var dialog = new OpenFileDialog { Filter = "Telink firmware (*.bin)|*.bin|All files (*.*)|*.*", CheckFileExists = true };
        if (dialog.ShowDialog(this) != true) return;
        _otaFile = dialog.FileName; OtaFileText.Text = _otaFile;
        var fi = new FileInfo(_otaFile); OtaFileInfoText.Text = $"{fi.Name}  {fi.Length:N0} bytes";
        UpdateControls();
    }

    private async void StartOta_Click(object sender, RoutedEventArgs e)
    {
        if (_otaInProgress) return;
        if (_bleTransport is null || !_bleTransport.IsConnected) { MessageBox.Show(this, "Connect through Bluetooth LE before OTA."); return; }
        if (!_bleTransport.HasOta) { MessageBox.Show(this, "The connected device does not expose the Telink OTA characteristic."); return; }
        if (string.IsNullOrWhiteSpace(_otaFile) || !File.Exists(_otaFile)) { MessageBox.Show(this, "Choose a valid firmware .bin first."); return; }
        if (MessageBox.Show(this, $"Start BLE OTA with:\n{_otaFile}\n\nDo not remove power during transfer.", "Confirm OTA", MessageBoxButton.YesNo, MessageBoxImage.Warning) != MessageBoxResult.Yes) return;

        _otaInProgress = true; _otaCts = new CancellationTokenSource(); StartOtaButton.IsEnabled = false; CancelOtaButton.IsEnabled = true;
        OtaProgressBar.Value = 0; OtaStatusText.Text = "Starting OTA...";
        await _operationGate.WaitAsync();
        try
        {
            var ota = new TelinkBleOtaClient(_bleTransport);
            var progress = new Progress<OtaProgress>(p => { OtaProgressBar.Value = p.Percent; OtaProgressText.Text = $"{p.Percent:F1}%  packets {p.SentPackets}/{p.TotalPackets}  bytes {p.SentBytes:N0}/{p.FirmwareBytes:N0}"; });
            Log($"OTA start: {_otaFile}");
            var completion = await ota.UpgradeAsync(_otaFile, progress, _otaCts.Token);
            OtaStatusText.Text = completion.Message;
            if (completion.ResultConfirmed && completion.ResultCode == 0) OtaProgressBar.Value = 100;
            Log(completion.Message);
        }
        catch (OperationCanceledException) { OtaStatusText.Text = "OTA cancelled locally. Reconnect before retrying."; Log(OtaStatusText.Text); }
        catch (Exception ex) { OtaStatusText.Text = "OTA failed: " + ex.Message; LogError(ex); }
        finally
        {
            _operationGate.Release(); _otaInProgress = false; _otaCts?.Dispose(); _otaCts = null;
            CancelOtaButton.IsEnabled = false; UpdateControls();
        }
    }

    private void CancelOta_Click(object sender, RoutedEventArgs e) => _otaCts?.Cancel();

    private async void RawRead_Click(object sender, RoutedEventArgs e)
    {
        await RunExclusiveAsync(async () =>
        {
            if (_client is null) return;
            var reg = ParseU16(RawRegisterText.Text, true);
            var qty = ushort.Parse(RawQuantityText.Text.Trim(), CultureInfo.InvariantCulture);
            var values = await _client.ReadRawAsync(reg, qty);
            var sb = new StringBuilder();
            for (var i = 0; i < values.Length; i++) sb.AppendLine($"0x{reg + i:X4} = 0x{values[i]:X4} ({values[i]})");
            RawResultText.Text = sb.ToString();
        });
    }

    private async void RawWrite_Click(object sender, RoutedEventArgs e)
    {
        await RunExclusiveAsync(async () =>
        {
            if (_client is null) return;
            var reg = ParseU16(RawWriteRegisterText.Text, true);
            var value = ParseU16(RawWriteValueText.Text, false);
            await _client.WriteRawAsync(reg, value);
            RawResultText.Text = $"Wrote 0x{reg:X4} = 0x{value:X4} ({value})";
        });
    }

    private static ushort ParseU16(string text, bool defaultHex)
    {
        text = text.Trim();
        if (text.StartsWith("0x", StringComparison.OrdinalIgnoreCase)) return ushort.Parse(text[2..], NumberStyles.HexNumber, CultureInfo.InvariantCulture);
        return ushort.Parse(text, defaultHex ? NumberStyles.HexNumber : NumberStyles.Integer, CultureInfo.InvariantCulture);
    }

    private async Task RunExclusiveAsync(Func<Task> action)
    {
        if (!IsConnected || _client is null) { MessageBox.Show(this, "Connect to the BMS first."); return; }
        await _operationGate.WaitAsync();
        try { await action(); }
        catch (Exception ex) { LogError(ex); MessageBox.Show(this, ex.Message, "Operation failed", MessageBoxButton.OK, MessageBoxImage.Error); }
        finally { _operationGate.Release(); }
    }

    private void TransportMode_Changed(object sender, RoutedEventArgs e) => UpdateControls();

    private void UpdateControls()
    {
        if (!IsInitialized) return;
        var connected = IsConnected;
        SerialConnectButton.Content = _transport is SerialBmsTransport && connected ? "Disconnect Serial" : "Connect Serial";
        BleConnectButton.Content = _transport is BleBmsTransport && connected ? "Disconnect BLE" : "Connect BLE";
        StartOtaButton.IsEnabled = !_otaInProgress && _bleTransport?.IsConnected == true && _bleTransport.HasOta && !string.IsNullOrWhiteSpace(_otaFile);
        SerialModeRadio.IsEnabled = !connected; BleModeRadio.IsEnabled = !connected;
    }

    private void Log(string message) { LogBox.AppendText($"[{DateTime.Now:HH:mm:ss.fff}] {message}{Environment.NewLine}"); LogBox.ScrollToEnd(); }
    private void LogError(Exception ex) => Log($"ERROR: {ex.GetType().Name}: {ex.Message}");

    private async void Window_Closing(object? sender, CancelEventArgs e)
    {
        _pollTimer.Stop(); _otaCts?.Cancel();
        try { await DisconnectCurrentAsync(); } catch { }
        _operationGate.Dispose();
    }
}
