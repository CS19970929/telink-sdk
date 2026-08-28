using System.Runtime.InteropServices;

namespace BmsHost.Core.Protocol;

public sealed class ModbusFrameAccumulator
{
    private readonly List<byte> _buffer = new(512);

    public void Reset() => _buffer.Clear();

    public byte[]? Push(ReadOnlySpan<byte> bytes)
    {
        for (var i = 0; i < bytes.Length; i++)
            _buffer.Add(bytes[i]);

        while (_buffer.Count >= 2)
        {
            var expected = ExpectedLength(CollectionsMarshal.AsSpan(_buffer));
            if (expected < 0)
            {
                _buffer.RemoveAt(0);
                continue;
            }
            if (expected == 0 || _buffer.Count < expected)
                return null;

            var candidate = _buffer.GetRange(0, expected).ToArray();
            if (ModbusCrc16.IsValid(candidate))
            {
                _buffer.RemoveRange(0, expected);
                return candidate;
            }

            _buffer.RemoveAt(0);
        }

        return null;
    }

    private static int ExpectedLength(ReadOnlySpan<byte> data)
    {
        if (data.Length < 2) return 0;
        var function = data[1];
        if ((function & 0x80) != 0) return 5;

        return function switch
        {
            0x03 when data.Length >= 3 => 3 + data[2] + 2,
            0x06 => 8,
            0x10 => 8,
            _ => -1,
        };
    }
}
