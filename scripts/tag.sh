#!/bin/bash
set -e

# Get version type from first argument or default to patch
VERSION_TYPE=${1:-patch}

# Get the latest tag or default to v0.0.0
LATEST_TAG=$(git tag --sort=-version:refname | head -n 1 || echo "v0.0.0")
echo "Latest tag: $LATEST_TAG"

# Extract version number (remove 'v' prefix)
VERSION=${LATEST_TAG#v}

# Split version into major, minor, patch
IFS='.' read -r MAJOR MINOR PATCH <<< "$VERSION"

# Default to 0 if empty
MAJOR=${MAJOR:-0}
MINOR=${MINOR:-0}
PATCH=${PATCH:-0}

# Determine version type (default to patch if invalid)
case "$VERSION_TYPE" in
  major)
    MAJOR=$((MAJOR + 1))
    MINOR=0
    PATCH=0
    ;;
  minor)
    MINOR=$((MINOR + 1))
    PATCH=0
    ;;
  patch|*)
    PATCH=$((PATCH + 1))
    ;;
esac

# Construct new tag
NEW_TAG="v${MAJOR}.${MINOR}.${PATCH}"
echo "Creating new tag: $NEW_TAG"

# Create the tag on the last commit
git tag "$NEW_TAG"

# Push tag to GitHub
echo "Pushing tag $NEW_TAG to GitHub..."
git push nicovsj-primary "$NEW_TAG"

echo "Successfully created and pushed tag $NEW_TAG"
