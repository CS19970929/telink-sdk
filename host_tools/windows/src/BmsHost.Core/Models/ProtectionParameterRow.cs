namespace BmsHost.Core.Models;

public enum ProtectionEncoding
{
    Direct,
    CurrentA,
    TemperatureC,
}

public sealed class ProtectionParameterRow
{
    public required string Name { get; init; }
    public required string Unit { get; init; }
    public required ProtectionEncoding Encoding { get; init; }
    public double L1 { get; set; }
    public double L2 { get; set; }
    public double L3 { get; set; }
    public double Recovery { get; set; }
    public double DelayMs { get; set; }

    public static double Decode(ushort raw, ProtectionEncoding encoding) => encoding switch
    {
        ProtectionEncoding.CurrentA => raw / 10.0,
        ProtectionEncoding.TemperatureC => (raw - 400) / 10.0,
        _ => raw,
    };

    public static ushort Encode(double value, ProtectionEncoding encoding)
    {
        var raw = encoding switch
        {
            ProtectionEncoding.CurrentA => Math.Round(value * 10.0),
            ProtectionEncoding.TemperatureC => Math.Round((value + 40.0) * 10.0),
            _ => Math.Round(value),
        };
        if (raw is < 0 or > ushort.MaxValue)
            throw new ArgumentOutOfRangeException(nameof(value), $"Value {value} cannot be encoded as UInt16.");
        return (ushort)raw;
    }
}
