using System.Text;
using BmsHost.Core.Models;
using BmsHost.Core.Protocol;
using BmsHost.Core.Transport;

namespace BmsHost.Core.Services;

public sealed class BmsClient
{
    private readonly ModbusRtuClient _modbus;

    public BmsClient(IBmsTransport transport)
    {
        Transport = transport;
        _modbus = new ModbusRtuClient(transport);
    }

    public IBmsTransport Transport { get; }
    public ModbusRtuClient Modbus => _modbus;

    public async Task<BmsSnapshot> ReadSnapshotAsync(CancellationToken cancellationToken = default)
    {
        var cells = await _modbus.ReadHoldingRegistersAsync(BmsRegisterMap.Cells, BmsRegisterMap.CellCount, cancellationToken);
        var summary = await _modbus.ReadHoldingRegistersAsync(BmsRegisterMap.CellMax, 22, cancellationToken);
        var realtime = await _modbus.ReadHoldingRegistersAsync(BmsRegisterMap.Realtime, BmsRegisterMap.RealtimeCount, cancellationToken);
        var afe = await _modbus.ReadHoldingRegistersAsync(BmsRegisterMap.AfeStatus, 2, cancellationToken);
        var protect = await _modbus.ReadHoldingRegistersAsync(BmsRegisterMap.ProtectionStatus, BmsRegisterMap.ProtectionStatusCount, cancellationToken);
        var serialPm = await _modbus.ReadHoldingRegistersAsync(BmsRegisterMap.SerialPm, BmsRegisterMap.SerialPmCount, cancellationToken);

        var currentA10 = unchecked((short)realtime[3]);
        var cellVolts = cells.Select(v => v / 1000.0).ToArray();
        var faultBits = (uint)(protect[8] | ((uint)protect[9] << 16));
        var sleepCount = (uint)(serialPm[4] | ((uint)serialPm[5] << 16));
        var wakeCount = (uint)(serialPm[6] | ((uint)serialPm[7] << 16));

        return new BmsSnapshot(
            DateTime.Now,
            realtime[2] / 100.0,
            currentA10 / 10.0,
            realtime[4],
            summary[21],
            cellVolts,
            summary[0] / 1000.0,
            summary[1] / 1000.0,
            summary[4] / 1000.0,
            DecodeLegacyTemperature(summary[6]),
            DecodeLegacyTemperature(summary[7]),
            DecodeLegacyTemperature(summary[16]),
            DecodeLegacyTemperature(summary[17]),
            (afe[0] & 0x0001) != 0,
            (afe[0] & 0x0002) != 0,
            afe[0], afe[1],
            protect[2], protect[3], protect[4], protect[5], protect[6],
            unchecked((short)protect[7]),
            faultBits,
            serialPm[2], sleepCount, wakeCount,
            serialPm.Length > 8 ? serialPm[8] : (ushort)0,
            serialPm.Length > 9 ? serialPm[9] : (ushort)0);
    }

    public async Task<DeviceInfo> ReadDeviceInfoAsync(CancellationToken cancellationToken = default)
    {
        var sn = DecodeAscii(await _modbus.ReadHoldingRegistersAsync(BmsRegisterMap.ProductSerial, BmsRegisterMap.ProductStringRegisterCount, cancellationToken));
        var hw = DecodeAscii(await _modbus.ReadHoldingRegistersAsync(BmsRegisterMap.ProductHardwareVersion, BmsRegisterMap.ProductStringRegisterCount, cancellationToken));
        var sw = DecodeAscii(await _modbus.ReadHoldingRegistersAsync(BmsRegisterMap.ProductSoftwareVersion, BmsRegisterMap.ProductStringRegisterCount, cancellationToken));
        var name = DecodeAscii(await _modbus.ReadHoldingRegistersAsync(BmsRegisterMap.BluetoothName, BmsRegisterMap.BluetoothNameCount, cancellationToken));
        return new DeviceInfo(sn, hw, sw, name);
    }

    public async Task SetBluetoothNameAsync(string value, CancellationToken cancellationToken = default)
    {
        if (string.IsNullOrWhiteSpace(value)) throw new ArgumentException("Bluetooth name is empty.", nameof(value));
        if (!value.StartsWith("BT_", StringComparison.Ordinal)) value = "BT_" + value;
        if (Encoding.ASCII.GetByteCount(value) > 24) throw new ArgumentException("Bluetooth name must be <= 24 ASCII bytes.", nameof(value));

        var bytes = new byte[24];
        Encoding.ASCII.GetBytes(value.AsSpan(), bytes.AsSpan());
        var regs = new ushort[12];
        for (var i = 0; i < regs.Length; i++)
            regs[i] = (ushort)((bytes[i * 2] << 8) | bytes[i * 2 + 1]);

        // Keep FC16 requests <= 20 bytes so the same write works over the
        // firmware's default BLE ATT payload without MTU negotiation.
        for (var off = 0; off < regs.Length; off += 5)
        {
            var count = Math.Min(5, regs.Length - off);
            await _modbus.WriteMultipleRegistersAsync(
                (ushort)(BmsRegisterMap.BluetoothName + off),
                regs.AsSpan(off, count).ToArray(), cancellationToken);
        }
    }

    public async Task<IReadOnlyList<ProtectionParameterRow>> ReadProtectionParametersAsync(CancellationToken cancellationToken = default)
    {
        var raw = await _modbus.ReadHoldingRegistersAsync(BmsRegisterMap.ProtectionLegacy, BmsRegisterMap.ProtectionLegacyCount, cancellationToken);
        var descriptors = ProtectionDescriptors();
        var rows = new List<ProtectionParameterRow>(descriptors.Length);
        for (var group = 0; group < descriptors.Length; group++)
        {
            var d = descriptors[group];
            var off = group * 5;
            rows.Add(new ProtectionParameterRow
            {
                Name = d.Name, Unit = d.Unit, Encoding = d.Encoding,
                L1 = ProtectionParameterRow.Decode(raw[off], d.Encoding),
                L2 = ProtectionParameterRow.Decode(raw[off + 1], d.Encoding),
                L3 = ProtectionParameterRow.Decode(raw[off + 2], d.Encoding),
                Recovery = ProtectionParameterRow.Decode(raw[off + 3], d.Encoding),
                DelayMs = raw[off + 4],
            });
        }
        return rows;
    }

    public async Task WriteProtectionParametersAsync(IReadOnlyList<ProtectionParameterRow> rows, CancellationToken cancellationToken = default)
    {
        if (rows.Count != 13) throw new ArgumentException("Expected 13 protection parameter groups.", nameof(rows));
        var raw = new ushort[65];
        for (var group = 0; group < rows.Count; group++)
        {
            var r = rows[group];
            var off = group * 5;
            raw[off] = ProtectionParameterRow.Encode(r.L1, r.Encoding);
            raw[off + 1] = ProtectionParameterRow.Encode(r.L2, r.Encoding);
            raw[off + 2] = ProtectionParameterRow.Encode(r.L3, r.Encoding);
            raw[off + 3] = ProtectionParameterRow.Encode(r.Recovery, r.Encoding);
            if (r.DelayMs is < 0 or > ushort.MaxValue)
                throw new ArgumentOutOfRangeException(nameof(rows), $"{r.Name} delay is out of range.");
            raw[off + 4] = (ushort)Math.Round(r.DelayMs);
        }

        // Five registers yield a 19-byte FC16 RTU request, fitting the default
        // BLE ATT value while preserving one complete protection group/transaction.
        for (var group = 0; group < 13; group++)
        {
            var off = group * 5;
            await _modbus.WriteMultipleRegistersAsync(
                (ushort)(BmsRegisterMap.ProtectionLegacy + off),
                raw.AsSpan(off, 5).ToArray(), cancellationToken);
        }
    }

    public Task<ushort[]> ReadRawAsync(ushort register, ushort quantity, CancellationToken cancellationToken = default) =>
        _modbus.ReadHoldingRegistersAsync(register, quantity, cancellationToken);

    public Task WriteRawAsync(ushort register, ushort value, CancellationToken cancellationToken = default) =>
        _modbus.WriteSingleRegisterAsync(register, value, cancellationToken);

    public Task SetMosAsync(bool on, CancellationToken cancellationToken = default) =>
        _modbus.WriteSingleRegisterAsync(BmsRegisterMap.Command, on ? (ushort)0x0011 : (ushort)0x0012, cancellationToken);

    private static double DecodeLegacyTemperature(ushort raw) => (raw - 400) / 10.0;

    private static string DecodeAscii(IEnumerable<ushort> regs)
    {
        var bytes = new List<byte>();
        foreach (var reg in regs) { bytes.Add((byte)(reg >> 8)); bytes.Add((byte)reg); }
        var zero = bytes.IndexOf(0);
        if (zero >= 0) bytes.RemoveRange(zero, bytes.Count - zero);
        return Encoding.ASCII.GetString(bytes.ToArray()).Trim();
    }

    private static (string Name, string Unit, ProtectionEncoding Encoding)[] ProtectionDescriptors() =>
        new (string Name, string Unit, ProtectionEncoding Encoding)[]
        {
            ("Cell OV", "mV", ProtectionEncoding.Direct),
            ("Cell UV", "mV", ProtectionEncoding.Direct),
            ("Bus OV", "mV", ProtectionEncoding.Direct),
            ("Bus UV", "mV", ProtectionEncoding.Direct),
            ("Charge OC", "A", ProtectionEncoding.CurrentA),
            ("Discharge OC", "A", ProtectionEncoding.CurrentA),
            ("Charge OT", "°C", ProtectionEncoding.TemperatureC),
            ("Charge UT", "°C", ProtectionEncoding.TemperatureC),
            ("Discharge OT", "°C", ProtectionEncoding.TemperatureC),
            ("Discharge UT", "°C", ProtectionEncoding.TemperatureC),
            ("MOS OT", "°C", ProtectionEncoding.TemperatureC),
            ("Cell Delta", "mV", ProtectionEncoding.Direct),
            ("SOC Low", "%", ProtectionEncoding.Direct),
        };
}
