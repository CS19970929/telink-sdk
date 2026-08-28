namespace BmsHost.Win.Models;

public sealed class BleDeviceItem
{
    public ulong Address { get; init; }
    public string Name { get; set; } = string.Empty;
    public short Rssi { get; set; }
    public string AddressText => string.Join(":", Enumerable.Range(0, 6)
        .Select(i => ((Address >> ((5 - i) * 8)) & 0xFF).ToString("X2")));
    public string Display => $"{(string.IsNullOrWhiteSpace(Name) ? "(unnamed)" : Name)}  {AddressText}  {Rssi} dBm";
}
