#!/bin/bash
docker compose -f docker/docker-compose.yml kill "$1"
