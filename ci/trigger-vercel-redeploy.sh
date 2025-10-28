#!/bin/bash
set -e

if [ -z "$VERCEL_TOKEN" ] || [ -z "$VERCEL_PROJECT_ID" ] || [ -z "$VERCEL_ORG_ID" ]; then
  echo "Skipping Vercel redeploy: Missing required secrets (VERCEL_TOKEN, VERCEL_PROJECT_ID, VERCEL_ORG_ID)"
  exit 0
fi

TAG=${GITHUB_REF#refs/tags/}
echo "Triggering Vercel redeploy for version $TAG..."

response=$(curl -s -X POST \
  "https://api.vercel.com/v13/deployments" \
  -H "Authorization: Bearer $VERCEL_TOKEN" \
  -H "Content-Type: application/json" \
  -d "{
    \"name\": \"$VERCEL_PROJECT_ID\",
    \"project\": \"$VERCEL_PROJECT_ID\",
    \"target\": \"production\",
    \"gitSource\": {
      \"type\": \"github\",
      \"repo\": \"$GITHUB_REPOSITORY\",
      \"ref\": \"$GITHUB_REF\",
      \"sha\": \"$GITHUB_SHA\"
    }
  }")

deployment_url=$(echo "$response" | jq -r '.url // empty')
if [ -n "$deployment_url" ] && [ "$deployment_url" != "null" ]; then
  echo "Vercel deployment triggered: $deployment_url"
else
  echo "Warning: Could not parse deployment URL from response"
  echo "Response: $response"
fi
