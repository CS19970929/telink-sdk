namespace BmsHost.Core.Protocol;

public static class ModbusCrc16
{
    public static ushort Compute(ReadOnlySpan<byte> data)
    {
        ushort crc = 0xFFFF;
        foreach (byte value in data)
        {
            crc ^= value;
            for (var bit = 0; bit < 8; bit++)
                crc = (crc & 1) != 0 ? (ushort)((crc >> 1) ^ 0xA001) : (ushort)(crc >> 1);
        }
        return crc;
    }

    public static bool IsValid(ReadOnlySpan<byte> frame)
    {
        if (frame.Length < 4) return false;
        var expected = Compute(frame[..^2]);
        var actual = (ushort)(frame[^2] | (frame[^1] << 8));
        return expected == actual;
    }

    public static byte[] Append(ReadOnlySpan<byte> payload)
    {
        var frame = new byte[payload.Length + 2];
        payload.CopyTo(frame);
        var crc = Compute(payload);
        frame[^2] = (byte)crc;
        frame[^1] = (byte)(crc >> 8);
        return frame;
    }
}
