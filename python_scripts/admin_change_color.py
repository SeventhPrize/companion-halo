import requests
import random
import argparse
import json

# Constants
N_COLOR = 10
MAC = "02:01:20:22:ADMIN"

# Construct argument parser
parser = argparse.ArgumentParser(description='Change all Companion Halo colors from command line.')
parser.add_argument('--color', help="Provide color as integer from 0 to 9.", required=False, choices=[str(num) for num in range(N_COLOR)])
parser.add_argument('--url', help="Provide Google Script Webapp URL.", required=False)
args = parser.parse_args()

# Parse color
if args.color is None:
    color = random.randint(0, N_COLOR - 1)
else:
    color = int(args.color)

# Parse webapp URL
if args.url is None:
    with open("secrets.json", "r") as file:
        json_object = json.load(file)
        file.close()
    url = json_object["webapp_url"]
else:
    url = str(args.url)

# Make request
rng = random.randint(1000, 9999)
url_query = f"{url}?fc={color}.{rng}.{MAC}"
req = requests.get(url_query)

# Report result
print(f"Admin changed color to {color} using request {url_query}")