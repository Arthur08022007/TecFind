# TecFind

Firmware autonome pour Seeed SenseCAP Indicator D1 Pro : affiche les prochains bus TEC directement sur l’écran. Il n’y a ni Home Assistant, ni broker MQTT, ni serveur local requis. L’appareil utilise son Wi-Fi pour interroger l’API Belgian Mobility.

Le D1 Pro n’a pas de GPS. À l’installation, on indique la latitude et la longitude de l’écran. L’outil `tools/find_stop.py` télécharge les horaires TEC et génère la liste des arrêts situés dans un rayon donné. Ensuite, l’arrêt se choisit directement sur l’écran tactile; le choix est mémorisé même après redémarrage.

## Prérequis

- SenseCAP Indicator D1 Pro et câble USB de données
- ESP-IDF 5.4.x avec `IDF_PATH` configuré
- Clé API Belgian Mobility avec accès aux flux TEC temps réel
- Coordonnées de l’endroit où l’écran sera installé

Le portail limite l’accès anonyme; en production, une clé API est requise. La clé se configure localement dans `idf.py menuconfig` et ne doit pas être ajoutée au dépôt.

## Mise en route

1. Générer les arrêts et horaires proches avec `python3 tools/find_stop.py LATITUDE LONGITUDE --radius-m 1000 --max-stops 12`. Les 12 arrêts les plus proches dans un rayon de 1 km seront proposés; adapte ces valeurs à ton lieu.
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
