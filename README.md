# TecFind

Firmware autonome pour Seeed SenseCAP Indicator D1 Pro : affiche les prochains bus TEC directement sur l’écran. Il n’y a ni Home Assistant, ni broker MQTT, ni serveur local requis. L’appareil utilise son Wi-Fi pour interroger l’API Belgian Mobility.

Le D1 Pro n’a pas de GPS. Pour choisir l’arrêt le plus proche, on configure une latitude/longitude à l’installation; l’outil `tools/find_stop.py` télécharge la liste officielle des arrêts, trouve le plus proche et donne son identifiant à saisir dans le menu de configuration ESP-IDF.

## Prérequis

- SenseCAP Indicator D1 Pro et câble USB de données
- ESP-IDF 5.4.x avec `IDF_PATH` configuré
- Clé API Belgian Mobility avec accès aux flux TEC temps réel
- Coordonnées de l’endroit où l’écran sera installé

Le portail limite l’accès anonyme; en production, une clé API est requise. La clé se configure localement dans `idf.py menuconfig` et ne doit pas être ajoutée au dépôt.

## Mise en route

1. Générer les horaires du lieu d’installation avec `python3 tools/find_stop.py LATITUDE LONGITUDE`.
2. Dans `idf.py menuconfig`, ouvrir **TecFind** et saisir la clé API.
3. Configurer le Wi-Fi dans l’écran tactile après le démarrage.
4. Compiler et flasher : `./dev build`, puis `./dev flash`.

Les horaires locaux viennent de GTFS. Le firmware les combine directement avec les mises à jour TEC GTFS-Realtime pour appliquer les heures estimées, les retards et les annulations. Si le flux temps réel est indisponible, l’écran garde les horaires planifiés et l’indique clairement.

## Interface

L’écran affiche les trois prochains départs sous forme de cartes (numéro de ligne coloré, destination, heure et minutes restantes). Les polices Inter incluent les accents et la ponctuation française; pour les régénérer : `./tools/gen_fonts.sh` (nécessite `lv_font_conv`). L’icône du bus est générée par `python3 tools/gen_bus_icon.py`.

## Sources

- [Catalogue officiel des données TEC](https://data.belgianmobility.io/fr/data.html?agency=tec)
- [Documentation GTFS et API Belgian Mobility](https://data.belgianmobility.io/fr/knowledge-base.html)
- Attribution requise : Belgian Mobility Company / LETEC, Open Data.

Le SenseCAP Indicator D1 Pro est un produit de Seeed Studio. TecFind est un projet indépendant non affilié au TEC.
