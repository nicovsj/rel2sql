#!/bin/bash
set -e

# Helper function to make API calls and return response
api_call() {
  local url="$1"
  local method="${2:-GET}"
  local data="${3:-}"
  local temp_file="/tmp/vercel_api_$$.json"

  local curl_args=(
    -s -w "%{http_code}"
    -o "$temp_file"
    -X "$method"
    -H "Authorization: Bearer $VERCEL_TOKEN"
    -H "Content-Type: application/json"
  )

  if [ -n "$data" ]; then
    curl_args+=(-d "$data")
  fi

  local http_code
  http_code=$(curl "${curl_args[@]}" "$url")
  local response
  response=$(cat "$temp_file" 2>/dev/null || echo "")
  rm -f "$temp_file"

  echo "$http_code|$response"
}

# Helper function to extract value from JSON response
json_extract() {
  local json="$1"
  local path="$2"
  echo "$json" | jq -r "$path // empty" 2>/dev/null || echo ""
}

# Helper function to show error and exit
error_exit() {
  echo "Error: $1" >&2
  if [ -n "$2" ]; then
    echo "Details: $2" >&2
  fi
  exit 1
}

# Check prerequisites
check_requirements() {
  if [ -z "$VERCEL_TOKEN" ] || [ -z "$VERCEL_PROJECT_ID" ]; then
    echo "Skipping Vercel redeploy: Missing required secrets (VERCEL_TOKEN, VERCEL_PROJECT_ID)"
    exit 0
  fi

  if ! command -v jq &> /dev/null; then
    error_exit "jq is required but not installed. Please install jq to continue."
  fi
}

# Get project name from Vercel API
get_project_name() {
  local url="https://api.vercel.com/v9/projects/$VERCEL_PROJECT_ID"
  if [ -n "$VERCEL_ORG_ID" ]; then
    url="${url}?teamId=$VERCEL_ORG_ID"
  fi

  echo "Fetching project information..." >&2
  local result
  result=$(api_call "$url" "GET")
  local http_code
  http_code=$(echo "$result" | cut -d'|' -f1)
  local response
  response=$(echo "$result" | cut -d'|' -f2-)

  if [ "$http_code" -eq 200 ]; then
    local name
    name=$(json_extract "$response" ".name")
    if [ -n "$name" ] && [ "$name" != "null" ]; then
      echo "Project: $name" >&2
      echo "$name"
    else
      echo "$VERCEL_PROJECT_ID"
    fi
  else
    echo "Warning: Could not fetch project info (HTTP $http_code), using project ID as name" >&2
    echo "$VERCEL_PROJECT_ID"
  fi
}

# Get latest deployment ID
get_latest_deployment_id() {
  local url="https://api.vercel.com/v6/deployments"
  local query_params="projectId=$VERCEL_PROJECT_ID&limit=1"

  if [ -n "$VERCEL_ORG_ID" ]; then
    url="${url}?teamId=$VERCEL_ORG_ID&${query_params}"
  else
    url="${url}?${query_params}"
  fi

  echo "Fetching latest deployment..." >&2
  local result
  result=$(api_call "$url" "GET")
  local http_code
  http_code=$(echo "$result" | cut -d'|' -f1)
  local response
  response=$(echo "$result" | cut -d'|' -f2-)

  if [ "$http_code" -ne 200 ]; then
    error_exit "Failed to fetch deployments (HTTP $http_code)" "$response"
  fi

  local deployment
  deployment=$(json_extract "$response" ".deployments[0]")
  if [ -z "$deployment" ] || [ "$deployment" == "null" ]; then
    error_exit "No deployments found for this project"
  fi

  local deployment_id
  deployment_id=$(echo "$deployment" | jq -r '.uid // .id // empty' 2>/dev/null || echo "")
  if [ -z "$deployment_id" ] || [ "$deployment_id" == "null" ]; then
    error_exit "Could not extract deployment ID from latest deployment"
  fi

  echo "Latest deployment ID: $deployment_id" >&2
  echo "$deployment_id"
}

# Trigger redeploy
trigger_redeploy() {
  local deployment_id="$1"
  local project_name="$2"

  local url="https://vercel.com/api/v13/deployments?forceNew=1"
  if [ -n "$VERCEL_ORG_ID" ]; then
    url="${url}&teamId=$VERCEL_ORG_ID"
  fi

  local payload
  payload=$(jq -n \
    --arg deploymentId "$deployment_id" \
    --arg name "$project_name" \
    '{deploymentId: $deploymentId, meta: {action: "redeploy"}, name: $name, target: "production"}')

  echo "Triggering redeploy..." >&2
  local result
  result=$(api_call "$url" "POST" "$payload")
  local http_code
  http_code=$(echo "$result" | cut -d'|' -f1)
  local response
  response=$(echo "$result" | cut -d'|' -f2-)

  if [ "$http_code" -lt 200 ] || [ "$http_code" -ge 300 ]; then
    local error_msg
    error_msg=$(json_extract "$response" ".error.message // .error")
    error_exit "Vercel API request failed (HTTP $http_code)" "${error_msg:-$response}"
  fi

  local deployment_url
  deployment_url=$(json_extract "$response" ".url")
  if [ -z "$deployment_url" ] || [ "$deployment_url" == "null" ]; then
    error_exit "Could not parse deployment URL from response" "$response"
  fi

  echo "✓ Vercel deployment triggered successfully: $deployment_url" >&2

  local new_deployment_id
  new_deployment_id=$(json_extract "$response" ".id")
  if [ -n "$new_deployment_id" ] && [ "$new_deployment_id" != "null" ]; then
    echo "Deployment ID: $new_deployment_id" >&2
  fi
}

# Main execution
main() {
  check_requirements

  echo "Triggering Vercel redeploy of latest deployment..." >&2

  local project_name
  project_name=$(get_project_name)
  local deployment_id
  deployment_id=$(get_latest_deployment_id)
  trigger_redeploy "$deployment_id" "$project_name"
}

main
