using System.IO;
using System.IO.Ports;
using BmsHost.Core.Protocol;
using BmsHost.Core.Transport;

namespace BmsHost.Win.Transport;

public sealed class SerialBmsTransport : IBmsTransport
{
    private readonly SerialPort _port;
    private readonly SemaphoreSlim _requestGate = new(1, 1);
    private readonly object _sync = new();
    private readonly ModbusFrameAccumulator _accumulator = new();
    private TaskCompletionSource<byte[]>? _responseTcs;

    public SerialBmsTransport(string portName, int baudRate)
    {
        _port = new SerialPort(portName, baudRate, Parity.None, 8, StopBits.One)
        {
            Handshake = Handshake.None,
            ReadTimeout = 100,
            WriteTimeout = 1000,
            DtrEnable = false,
            RtsEnable = false,
        };
        _port.DataReceived += OnDataReceived;
    }

    public string Name => $"Serial {_port.PortName} @{_port.BaudRate}";
    public bool IsConnected => _port.IsOpen;
    public event EventHandler? Disconnected;

    public Task ConnectAsync()
    {
        _port.Open();
        _port.DiscardInBuffer();
        _port.DiscardOutBuffer();
        return Task.CompletedTask;
    }

    public async Task<byte[]> RequestAsync(ReadOnlyMemory<byte> request, TimeSpan timeout, CancellationToken cancellationToken = default)
    {
        await _requestGate.WaitAsync(cancellationToken);
        try
        {
            TaskCompletionSource<byte[]> tcs;
            lock (_sync)
            {
                if (!_port.IsOpen) throw new IOException("Serial port is not open.");
                _accumulator.Reset();
                tcs = new TaskCompletionSource<byte[]>(TaskCreationOptions.RunContinuationsAsynchronously);
                _responseTcs = tcs;
            }

            try
            {
                var bytes = request.ToArray();
                _port.Write(bytes, 0, bytes.Length);
                return await tcs.Task.WaitAsync(timeout, cancellationToken);
            }
            catch (TimeoutException)
            {
                throw new TimeoutException($"No Modbus response on {_port.PortName} within {timeout.TotalMilliseconds:0} ms.");
            }
            finally
            {
                lock (_sync)
                {
                    if (ReferenceEquals(_responseTcs, tcs)) _responseTcs = null;
                }
            }
        }
        finally
        {
            _requestGate.Release();
        }
    }

    private void OnDataReceived(object? sender, SerialDataReceivedEventArgs e)
    {
        try
        {
            while (_port.IsOpen && _port.BytesToRead > 0)
            {
                var buffer = new byte[_port.BytesToRead];
                var read = _port.Read(buffer, 0, buffer.Length);
                if (read <= 0) return;
                lock (_sync)
                {
                    var frame = _accumulator.Push(buffer.AsSpan(0, read));
                    if (frame is not null) _responseTcs?.TrySetResult(frame);
                }
            }
        }
        catch (Exception ex) when (ex is IOException or InvalidOperationException)
        {
            lock (_sync) _responseTcs?.TrySetException(ex);
            Disconnected?.Invoke(this, EventArgs.Empty);
        }
    }

    public Task DisconnectAsync()
    {
        lock (_sync)
        {
            _accumulator.Reset();
            _responseTcs?.TrySetException(new IOException("Serial port disconnected."));
            _responseTcs = null;
        }
        if (_port.IsOpen) _port.Close();
        return Task.CompletedTask;
    }

    public async ValueTask DisposeAsync()
    {
        await DisconnectAsync();
        _port.DataReceived -= OnDataReceived;
        _port.Dispose();
        _requestGate.Dispose();
    }
}
