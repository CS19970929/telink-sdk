import 'package:flutter/services.dart';

class ParameterMetadata {
  const ParameterMetadata(this.key, this.unit);
  final String key;
  final String unit;
}

class ParameterCatalog {
  const ParameterCatalog(this.entries);
  const ParameterCatalog.empty() : entries = const <int, ParameterMetadata>{};

  final Map<int, ParameterMetadata> entries;

  static Future<ParameterCatalog> load() async {
    final yaml = await rootBundle.loadString('assets/bms_schema.yaml');
    final expression = RegExp(r'id: 0x([0-9a-fA-F]+), key: ([^,]+), type: [^,]+, unit: ([^ }]+)');
    final entries = <int, ParameterMetadata>{};
    for (final match in expression.allMatches(yaml)) {
      final id = int.parse(match.group(1)!, radix: 16);
      entries[id] = ParameterMetadata(match.group(2)!, match.group(3)!);
    }
    return ParameterCatalog(entries);
  }

  ParameterMetadata metadataFor(int id) => entries[id] ?? ParameterMetadata('parameter.0x${id.toRadixString(16)}', '');
}
