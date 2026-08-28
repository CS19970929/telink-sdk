using System.IO;
using BmsHost.Core.Ota;
using BmsHost.Core.Protocol;
using BmsHost.Core.Transport;
using BmsHost.Win.Models;
using Windows.Devices.Bluetooth;
using Windows.Devices.Bluetooth.Advertisement;
using Windows.Devices.Bluetooth.GenericAttributeProfile;
using Windows.Storage.Streams;

namespace BmsHost.Win.Transport;

public sealed class BleBmsTransport : IBmsTransport
{
    public static readonly Guid NusServiceUuid = Guid.Parse("6E400001-B5A3-F393-E0A9-E50E24DCCA9E");
    public static readonly Guid NusWriteUuid = Guid.Parse("6E400002-B5A3-F393-E0A9-E50E24DCCA9E");
    public static readonly Guid NusNotifyUuid = Guid.Parse("6E400003-B5A3-F393-E0A9-E50E24DCCA9E");
    public static readonly Guid OtaServiceUuid = Guid.Parse("00010203-0405-0607-0809-0A0B0C0D1912");
    public static readonly Guid OtaCharacteristicUuid = Guid.Parse("00010203-0405-0607-0809-0A0B0C0D2B12");

    private readonly SemaphoreSlim _requestGate = new(1, 1);
    private readonly object _sync = new();
    private readonly ModbusFrameAccumulator _accumulator = new();
    private BluetoothLEDevice? _device;
    private GattDeviceService? _nusService;
    private GattCharacteristic? _nusWrite;
    private GattCharacteristic? _nusNotify;
    private GattDeviceService? _otaService;
    private GattCharacteristic? _otaCharacteristic;
    private TaskCompletionSource<byte[]>? _responseTcs;
    private TaskCompletionSource<byte>? _otaResultTcs;
    private byte? _lastOtaResult;

    public string Name => _device is null ? "BLE" : $"BLE {_device.Name}";
    public bool IsConnected => _device?.ConnectionStatus == BluetoothConnectionStatus.Connected;
    public bool HasOta => _otaCharacteristic is not null;
    public event EventHandler? Disconnected;

    public static async Task<IReadOnlyList<BleDeviceItem>> ScanAsync(TimeSpan duration, CancellationToken cancellationToken = default)
    {
        var devices = new Dictionary<ulong, BleDeviceItem>();
        var gate = new object();
        var watcher = new BluetoothLEAdvertisementWatcher { ScanningMode = BluetoothLEScanningMode.Active };
        watcher.Received += (_, args) =>
        {
            var name = args.Advertisement.LocalName ?? string.Empty;
            lock (gate)
            {
                if (!devices.TryGetValue(args.BluetoothAddress, out var item))
                {
                    item = new BleDeviceItem { Address = args.BluetoothAddress };
                    devices[args.BluetoothAddress] = item;
                }
                if (!string.IsNullOrWhiteSpace(name)) item.Name = name;
                item.Rssi = args.RawSignalStrengthInDBm;
            }
        };

        watcher.Start();
        try { await Task.Delay(duration, cancellationToken); }
        finally { watcher.Stop(); }
        lock (gate) return devices.Values.OrderByDescending(d => d.Rssi).ToArray();
    }

    public async Task ConnectAsync(ulong address, CancellationToken cancellationToken = default)
    {
        cancellationToken.ThrowIfCancellationRequested();
        _device = await BluetoothLEDevice.FromBluetoothAddressAsync(address)
            ?? throw new IOException("Windows could not open the BLE device.");
        _device.ConnectionStatusChanged += OnConnectionStatusChanged;

        var services = await _device.GetGattServicesForUuidAsync(NusServiceUuid, BluetoothCacheMode.Uncached);
        if (services.Status != GattCommunicationStatus.Success || services.Services.Count == 0)
            throw new IOException("BMS NUS service 6E400001... not found.");
        _nusService = services.Services[0];
        _nusWrite = await FindCharacteristicAsync(_nusService, NusWriteUuid);
        _nusNotify = await FindCharacteristicAsync(_nusService, NusNotifyUuid);
        _nusNotify!.ValueChanged += OnNusValueChanged;
        var ccc = await _nusNotify.WriteClientCharacteristicConfigurationDescriptorAsync(GattClientCharacteristicConfigurationDescriptorValue.Notify);
        if (ccc != GattCommunicationStatus.Success)
            throw new IOException($"Failed to enable BMS notifications: {ccc}.");

        var otaServices = await _device.GetGattServicesForUuidAsync(OtaServiceUuid, BluetoothCacheMode.Uncached);
        if (otaServices.Status == GattCommunicationStatus.Success && otaServices.Services.Count > 0)
        {
            _otaService = otaServices.Services[0];
            _otaCharacteristic = await FindCharacteristicAsync(_otaService, OtaCharacteristicUuid, required: false);
        }
    }

    public async Task<byte[]> RequestAsync(ReadOnlyMemory<byte> request, TimeSpan timeout, CancellationToken cancellationToken = default)
    {
        await _requestGate.WaitAsync(cancellationToken);
        try
        {
            TaskCompletionSource<byte[]> tcs;
            GattCharacteristic nusWrite;
            lock (_sync)
            {
                if (!IsConnected || _nusWrite is null) throw new IOException("BLE BMS is not connected.");
                _accumulator.Reset();
                tcs = new TaskCompletionSource<byte[]>(TaskCreationOptions.RunContinuationsAsynchronously);
                _responseTcs = tcs;
                nusWrite = _nusWrite;
            }

            try
            {
                var status = await WriteCharacteristicAsync(nusWrite, request, GattWriteOption.WriteWithResponse);
                if (status != GattCommunicationStatus.Success) throw new IOException($"BLE NUS write failed: {status}.");
                return await tcs.Task.WaitAsync(timeout, cancellationToken);
            }
            catch (TimeoutException) { throw new TimeoutException($"No BLE Modbus notification within {timeout.TotalMilliseconds:0} ms."); }
            finally
            {
                lock (_sync) { if (ReferenceEquals(_responseTcs, tcs)) _responseTcs = null; }
            }
        }
        finally { _requestGate.Release(); }
    }

    public async Task PrepareOtaAsync(CancellationToken cancellationToken = default)
    {
        if (!IsConnected || _otaCharacteristic is null)
            throw new InvalidOperationException("Telink OTA GATT characteristic was not discovered.");
        cancellationToken.ThrowIfCancellationRequested();
        _lastOtaResult = null;
        _otaCharacteristic.ValueChanged -= OnOtaValueChanged;
        _otaCharacteristic.ValueChanged += OnOtaValueChanged;
        var status = await _otaCharacteristic.WriteClientCharacteristicConfigurationDescriptorAsync(GattClientCharacteristicConfigurationDescriptorValue.Notify);
        if (status != GattCommunicationStatus.Success) throw new IOException($"Failed to enable OTA notification: {status}.");
    }

    public async Task WriteOtaAsync(ReadOnlyMemory<byte> data, bool withResponse, CancellationToken cancellationToken = default)
    {
        if (!IsConnected || _otaCharacteristic is null) throw new InvalidOperationException("OTA channel is unavailable.");
        cancellationToken.ThrowIfCancellationRequested();
        var status = await WriteCharacteristicAsync(_otaCharacteristic, data,
            withResponse ? GattWriteOption.WriteWithResponse : GattWriteOption.WriteWithoutResponse);
        if (status != GattCommunicationStatus.Success) throw new IOException($"OTA BLE write failed: {status}.");
    }

    public async Task<byte?> WaitForOtaResultAsync(TimeSpan timeout, CancellationToken cancellationToken = default)
    {
        TaskCompletionSource<byte> tcs;
        lock (_sync)
        {
            if (_lastOtaResult.HasValue) return _lastOtaResult.Value;
            tcs = new TaskCompletionSource<byte>(TaskCreationOptions.RunContinuationsAsynchronously);
            _otaResultTcs = tcs;
        }
        try { return await tcs.Task.WaitAsync(timeout, cancellationToken); }
        catch (TimeoutException) { return null; }
        finally { lock (_sync) { if (ReferenceEquals(_otaResultTcs, tcs)) _otaResultTcs = null; } }
    }

    private void OnNusValueChanged(GattCharacteristic sender, GattValueChangedEventArgs args)
    {
        var bytes = BufferToBytes(args.CharacteristicValue);
        lock (_sync)
        {
            var frame = _accumulator.Push(bytes);
            if (frame is not null) _responseTcs?.TrySetResult(frame);
        }
    }

    private void OnOtaValueChanged(GattCharacteristic sender, GattValueChangedEventArgs args)
    {
        var bytes = BufferToBytes(args.CharacteristicValue);
        if (!TelinkOtaProtocol.TryParseResult(bytes, out var result)) return;
        lock (_sync)
        {
            _lastOtaResult = result;
            _otaResultTcs?.TrySetResult(result);
        }
    }

    private void OnConnectionStatusChanged(BluetoothLEDevice sender, object args)
    {
        if (sender.ConnectionStatus != BluetoothConnectionStatus.Disconnected) return;
        lock (_sync)
        {
            var error = new IOException("BLE disconnected.");
            _responseTcs?.TrySetException(error);
            _otaResultTcs?.TrySetException(error);
        }
        Disconnected?.Invoke(this, EventArgs.Empty);
    }

    private static async Task<GattCharacteristic?> FindCharacteristicAsync(GattDeviceService service, Guid uuid, bool required = true)
    {
        var result = await service.GetCharacteristicsForUuidAsync(uuid, BluetoothCacheMode.Uncached);
        if (result.Status == GattCommunicationStatus.Success && result.Characteristics.Count > 0)
            return result.Characteristics[0];
        if (required) throw new IOException($"GATT characteristic {uuid} not found.");
        return null;
    }

    private static async Task<GattCommunicationStatus> WriteCharacteristicAsync(GattCharacteristic characteristic, ReadOnlyMemory<byte> data, GattWriteOption option)
    {
        using var writer = new DataWriter();
        writer.WriteBytes(data.ToArray());
        var buffer = writer.DetachBuffer();
        var result = await characteristic.WriteValueWithResultAsync(buffer, option);
        return result.Status;
    }

    private static byte[] BufferToBytes(IBuffer buffer)
    {
        using var reader = DataReader.FromBuffer(buffer);
        var bytes = new byte[reader.UnconsumedBufferLength];
        reader.ReadBytes(bytes);
        return bytes;
    }

    public async Task DisconnectAsync()
    {
        lock (_sync)
        {
            var error = new IOException("BLE disconnected.");
            _accumulator.Reset();
            _responseTcs?.TrySetException(error);
            _responseTcs = null;
            _otaResultTcs?.TrySetException(error);
            _otaResultTcs = null;
        }
        if (_nusNotify is not null)
        {
            _nusNotify.ValueChanged -= OnNusValueChanged;
            try { await _nusNotify.WriteClientCharacteristicConfigurationDescriptorAsync(GattClientCharacteristicConfigurationDescriptorValue.None); }
            catch { }
        }
        if (_otaCharacteristic is not null) _otaCharacteristic.ValueChanged -= OnOtaValueChanged;
        if (_device is not null) _device.ConnectionStatusChanged -= OnConnectionStatusChanged;
        _otaCharacteristic = null;
        _otaService?.Dispose(); _otaService = null;
        _nusWrite = null; _nusNotify = null;
        _nusService?.Dispose(); _nusService = null;
        _device?.Dispose(); _device = null;
    }

    public async ValueTask DisposeAsync()
    {
        await DisconnectAsync();
        _requestGate.Dispose();
    }
}
