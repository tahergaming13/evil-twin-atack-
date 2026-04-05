# Exercise 2: GitHub Actions Basics

**Objective:** Create your first GitHub Actions workflow and understand the structure.

**Duration:** 20 minutes

---

## Prerequisites

Before starting, make sure you have:

- ✅ A GitHub account
- ✅ Git installed on your computer
- ✅ Node.js installed (version 18 or 20)
- ✅ A code editor (VS Code recommended)

---

## Overview

In this exercise, you'll:
- Verify your Node.js application works
- Create your first GitHub Actions workflow file
- Understand each part of the workflow
- Trigger your first CI pipeline

---

## Step 1: Check Your Setup

First, let's verify everything is installed correctly:

```bash
# Check Node.js version
node --version

# Check npm version
npm --version

# Check git version
git --version
```

You should see version numbers like:
- Node.js: v18.x.x or v20.x.x
- npm: 9.x.x or 10.x.x
- git: 2.x.x

---

## Step 2: Test the Application

We've created a simple Node.js app for you. Let's test it:

```bash
# Go to the exercise folder
cd exercise-02-github-actions

# Install dependencies
npm install

# Start the server
npm start
```

Your server should start. Keep it running and open a **new terminal window** to test it:

```bash
# Test the main endpoint
curl http://localhost:3000

# Expected output:
# {"message":"Hello from CI/CD Pipeline!","timestamp":"2024-...","version":"1.0.0"}

# Test the health endpoint
curl http://localhost:3000/health

# Expected output:
# {"status":"healthy"}
```

**Stop the server** (Ctrl+C) when you're done testing.

---

## Step 3: Initialize Git (If Not Already)

If your project isn't already a git repository:

```bash
# Initialize git
git init

# Create a main branch
git checkout -b main
```

---

## Step 4: Create the Workflow Directory

GitHub Actions looks for workflows in the `.github/workflows` directory:

```bash
# Create the directory
mkdir -p .github/workflows
```

---

## Step 5: Create Your First Workflow File

Create a file called `.github/workflows/ci.yml` with this content:

```yaml
name: CI Pipeline

on:
  push:
    branches: [main]
  pull_request:
    branches: [main]

jobs:
  build:
    runs-on: ubuntu-latest

    steps:
      - name: Checkout code
        uses: actions/checkout@v4

      - name: Setup Node.js
        uses: actions/setup-node@v4
        with:
          node-version: '20'

      - name: Install dependencies
        run: npm ci

      - name: Run tests
        run: npm test
```

---

## Step 6: Understanding the Structure

Let's break down each part:

```yaml
name: CI Pipeline          # A friendly name for your workflow

on:                        # TRIGGERS - When does this run?
  push:
    branches: [main]       # When code is pushed to main
  pull_request:
    branches: [main]       # When a PR is opened to main

jobs:                      # JOBS - What to run
  build:                   # Job name
    runs-on: ubuntu-latest # The computer to run on

    steps:                 # STEPS - Individual tasks
      - name: Checkout code
        uses: actions/checkout@v4

      - name: Setup Node.js
        uses: actions/setup-node@v4
        with:
          node-version: '20'

      - name: Install dependencies
        run: npm ci

      - name: Run tests
        run: npm test
```

---

## Step 7: Commit and Push to GitHub

```bash
# Add all files
git add .

# Commit with a message
git commit -m "Add first CI pipeline"

# If you have a remote repository:
git push origin main
```

If you don't have a remote yet, create one on GitHub:

1. Go to github.com
2. Click "+" → "New repository"
3. Name it "cicd-workshop"
4. Don't initialize with README
5. Copy the commands GitHub shows you
6. Run them in your terminal

---

## Step 8: Watch Your Pipeline Run

After pushing, go to your GitHub repository:

1. Click the **Actions** tab
2. You should see your workflow running!
3. Click on it to see each step
4. Wait for all steps to turn green ✅

---

## Step 9: Create a Pull Request

Let's see the pipeline run on a PR:

```bash
# Create a new branch
git checkout -b feature/add-greeting

# Edit server.js to add a new endpoint (we'll show you how)
# Then commit and push
git add .
git commit -m "Add greeting endpoint"
git push -u origin feature/add-greeting
```

Now create a Pull Request on GitHub:
1. You'll see a yellow banner "Compare & pull request"
2. Click "Create pull request"
3. Watch the CI run on the PR!

---

## Important Concepts

### What are Actions?

**Actions** are reusable building blocks. Think of them like functions:

```yaml
# This downloads your code
- uses: actions/checkout@v4

# This installs Node.js
- uses: actions/setup-node@v4
```

### Versions: Why @v4?

```yaml
# ❌ Bad - could break
uses: actions/checkout@master

# ✅ Good - stable
uses: actions/checkout@v4
```

---

## Verification Checklist

- [ ] Node.js and npm are installed
- [ ] App runs locally (`npm start`)
- [ ] Workflow file exists at `.github/workflows/ci.yml`
- [ ] Pushed to GitHub
- [ ] Saw workflow run in Actions tab
- [ ] Created a PR and saw CI run

---

## What You've Learned

✅ Creating a GitHub Actions workflow  
✅ Understanding workflow structure  
✅ Using marketplace actions  
✅ Triggering workflows on push/PR  

---

## Troubleshooting

**Problem:** "bash: npm: command not found"
**Solution:** Install Node.js from nodejs.org

**Problem:** Workflow not running
**Solution:** Check that:
- File is in `.github/workflows/`
- YAML syntax is correct
- You're pushing to the correct branch

**Problem:** Tests failing in CI but passing locally
**Solution:** Check that Node.js versions match

---

## Next Steps

Move on to [Exercise 3: Building Workflows](../exercise-03-workflows/)
