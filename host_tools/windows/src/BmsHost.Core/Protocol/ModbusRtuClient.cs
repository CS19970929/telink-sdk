using BmsHost.Core.Transport;

namespace BmsHost.Core.Protocol;

public sealed class ModbusRtuClient
{
    private readonly IBmsTransport _transport;

    public byte SlaveAddress { get; set; } = 0x01;
    public int RetryCount { get; set; } = 3;
    public TimeSpan RequestTimeout { get; set; } = TimeSpan.FromMilliseconds(900);
    public TimeSpan RetryDelay { get; set; } = TimeSpan.FromMilliseconds(180);

    public ModbusRtuClient(IBmsTransport transport) => _transport = transport;

    public async Task<ushort[]> ReadHoldingRegistersAsync(ushort start, ushort quantity, CancellationToken cancellationToken = default)
    {
        if (quantity is 0 or > 125) throw new ArgumentOutOfRangeException(nameof(quantity));

        // Keep async methods free of Span/stackalloc locals for C# 11 / .NET 7.
        var pdu = new byte[6];
        pdu[0] = SlaveAddress;
        pdu[1] = 0x03;
        pdu[2] = (byte)(start >> 8);
        pdu[3] = (byte)start;
        pdu[4] = (byte)(quantity >> 8);
        pdu[5] = (byte)quantity;

        var response = await ExecuteAsync(ModbusCrc16.Append(pdu), 0x03, cancellationToken);
        if (response.Length != 5 + quantity * 2 || response[2] != quantity * 2)
            throw new InvalidDataException("Unexpected FC03 response length.");

        var values = new ushort[quantity];
        for (var i = 0; i < quantity; i++)
            values[i] = (ushort)((response[3 + i * 2] << 8) | response[4 + i * 2]);
        return values;
    }

    public async Task WriteSingleRegisterAsync(ushort register, ushort value, CancellationToken cancellationToken = default)
    {
        // Do not use Span/stackalloc locals here: ref-struct locals are not
        // permitted in async methods by the C# 11 compiler used with .NET 7.
        var pdu = new byte[6];
        pdu[0] = SlaveAddress;
        pdu[1] = 0x06;
        pdu[2] = (byte)(register >> 8);
        pdu[3] = (byte)register;
        pdu[4] = (byte)(value >> 8);
        pdu[5] = (byte)value;

        var request = ModbusCrc16.Append(pdu);
        var response = await ExecuteAsync(request, 0x06, cancellationToken);
        if (response.Length < 6 || !PrefixEquals(response, request, 6))
            throw new InvalidDataException("FC06 echo mismatch.");
    }

    public async Task WriteMultipleRegistersAsync(ushort start, IReadOnlyList<ushort> values, CancellationToken cancellationToken = default)
    {
        if (values.Count is 0 or > 123) throw new ArgumentOutOfRangeException(nameof(values));
        var payload = new byte[7 + values.Count * 2];
        payload[0] = SlaveAddress;
        payload[1] = 0x10;
        payload[2] = (byte)(start >> 8);
        payload[3] = (byte)start;
        payload[4] = (byte)(values.Count >> 8);
        payload[5] = (byte)values.Count;
        payload[6] = (byte)(values.Count * 2);
        for (var i = 0; i < values.Count; i++)
        {
            payload[7 + i * 2] = (byte)(values[i] >> 8);
            payload[8 + i * 2] = (byte)values[i];
        }

        var response = await ExecuteAsync(ModbusCrc16.Append(payload), 0x10, cancellationToken);
        if (response.Length != 8) throw new InvalidDataException("Unexpected FC10 response length.");
        var echoedStart = (ushort)((response[2] << 8) | response[3]);
        var echoedQty = (ushort)((response[4] << 8) | response[5]);
        if (echoedStart != start || echoedQty != values.Count)
            throw new InvalidDataException("FC10 echo mismatch.");
    }

    private async Task<byte[]> ExecuteAsync(byte[] request, byte function, CancellationToken cancellationToken)
    {
        Exception? last = null;
        var attempts = Math.Max(1, RetryCount);
        for (var attempt = 1; attempt <= attempts; attempt++)
        {
            try
            {
                var response = await _transport.RequestAsync(request, RequestTimeout, cancellationToken);
                ValidateResponse(response, function);
                return response;
            }
            catch (Exception ex) when (ex is TimeoutException or IOException or InvalidDataException)
            {
                last = ex;
                if (attempt >= attempts) break;
                await Task.Delay(RetryDelay, cancellationToken);
            }
        }
        throw new IOException($"Modbus request failed after {attempts} attempt(s).", last);
    }

    private void ValidateResponse(ReadOnlySpan<byte> response, byte function)
    {
        if (response.Length < 5) throw new InvalidDataException("Modbus response too short.");
        if (!ModbusCrc16.IsValid(response)) throw new InvalidDataException("Modbus CRC mismatch.");
        if (response[0] != SlaveAddress) throw new InvalidDataException("Unexpected Modbus slave address.");
        if (response[1] == (function | 0x80))
            throw new IOException($"Modbus exception 0x{response[2]:X2} for function 0x{function:X2}.");
        if (response[1] != function)
            throw new InvalidDataException($"Unexpected Modbus function 0x{response[1]:X2}.");
    }

    private static bool PrefixEquals(byte[] left, byte[] right, int count)
    {
        if (left.Length < count || right.Length < count) return false;
        for (var i = 0; i < count; i++)
        {
            if (left[i] != right[i]) return false;
        }
        return true;
    }
}
